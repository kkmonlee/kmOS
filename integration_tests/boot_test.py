#!/usr/bin/env python3
import argparse
import json
from pathlib import Path
import re
import shutil
import socket
import subprocess
import tempfile
import time


class VM:
    def __init__(self, args, name, ram=64, machine="pc", append="", module=None):
        self.temp = tempfile.TemporaryDirectory(prefix="kmos-", dir="/tmp")
        if args.iso:
            name = "iso-" + name
        self.log = args.results / f"{name}.log"
        self.errors = args.results / f"{name}-qemu.log"
        self.output = self.log.open("wb")
        self.error_output = self.errors.open("wb")
        qmp_path = str(Path(self.temp.name) / "qmp")
        command = [args.qemu, "-m", f"{ram}M", "-machine", machine,
                   "-display", "none", "-serial", "stdio", "-monitor", "none",
                   "-qmp", f"unix:{qmp_path},server=on,wait=off", "-no-reboot",
                   "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04"]
        if args.iso:
            command += ["-cdrom", str(args.iso), "-boot", "d"]
        else:
            command += ["-kernel", str(args.kernel)]
            if append:
                command += ["-append", append]
            if module:
                command += ["-initrd", str(module)]
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE,
                                        stdout=self.output, stderr=self.error_output)
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.settimeout(5)
        try:
            deadline = time.monotonic() + 5
            while True:
                try:
                    self.socket.connect(qmp_path)
                    break
                except (FileNotFoundError, ConnectionRefusedError):
                    if self.process.poll() is not None or time.monotonic() > deadline:
                        raise AssertionError(self.errors.read_text())
                    time.sleep(0.01)
            self.qmp = self.socket.makefile("rwb", buffering=0)
            self.receive()
            self.command("qmp_capabilities")
        except BaseException:
            self.close()
            raise

    def receive(self):
        while True:
            line = self.qmp.readline()
            if not line:
                raise AssertionError("QEMU disconnected")
            message = json.loads(line)
            if "event" not in message:
                return message

    def command(self, execute, arguments=None):
        request = {"execute": execute}
        if arguments:
            request["arguments"] = arguments
        self.qmp.write(json.dumps(request).encode() + b"\n")
        response = self.receive()
        assert "error" not in response, response
        return response.get("return")

    def monitor(self, command):
        return self.command("human-monitor-command", {"command-line": command})

    def text(self):
        return self.log.read_text(errors="replace")

    def wait(self, predicate, timeout=10):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            text = self.text()
            if predicate(text):
                return text
            if self.process.poll() is not None:
                break
            time.sleep(0.01)
        raise AssertionError(f"{self.log.name}: timeout or early exit\n{self.text()[-3000:]}\n"
                             f"{self.errors.read_text()}")

    def serial_command(self, command):
        count = self.text().count("kmOS:/$ ")
        self.process.stdin.write((command + "\n").encode())
        self.process.stdin.flush()
        return self.wait(lambda text: text.count("kmOS:/$ ") > count)

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=2)
        if hasattr(self, "qmp"):
            self.qmp.close()
        self.socket.close()
        self.process.stdin.close()
        self.output.close()
        self.error_output.close()
        self.temp.cleanup()

    def __enter__(self):
        return self

    def __exit__(self, *unused):
        self.close()


def boot(args, ram, machine="pc"):
    with VM(args, f"boot-{machine}-{ram}", ram, machine) as vm:
        vm.wait(lambda text: "[BOOT] Ready" in text and "kmOS:/$ " in text)
        assert "[CPU] Exception frames verified" in vm.text()
        assert "[VMM] Page tables verified" in vm.text()
        registers = vm.monitor("info registers")
        cr0 = int(re.search(r"CR0=([0-9a-fA-F]+)", registers)[1], 16)
        assert cr0 & 0x80010000 == 0x80010000, registers
        text = vm.serial_command("echo serialok")
        assert "\nserialok\n" in text, text
        count = text.count("kmOS:/$ ")
        for char in "echo keyboardok\n":
            key = {" ": "spc", "\n": "ret"}.get(char, char)
            vm.monitor(f"sendkey {key} 20")
            time.sleep(0.04)
        text = vm.wait(lambda text: text.count("kmOS:/$ ") > count)
        assert "\nkeyboardok\n" in text, text
        for command in ("help", "mem", "ps", "uptime"):
            text = vm.serial_command(command)
        ticks = int(re.findall(r"Timer ticks: (\d+)", text)[-1])
        time.sleep(0.15)
        text = vm.serial_command("uptime")
        assert int(re.findall(r"Timer ticks: (\d+)", text)[-1]) > ticks, text
        assert "kernel_main" in text and "idle" in text, text
        assert "[PANIC]" not in text, text
    print(f"PASS boot, paging, keyboard, serial and timer: {machine}, {ram} MB", flush=True)


def paging(args, ram, module=None):
    with VM(args, f"paging-{ram}" + ("-module" if module else ""), ram,
            append="test=paging", module=module) as vm:
        text = vm.wait(lambda text: "[TEST] passed" in text)
        assert vm.process.wait(timeout=2) == 33, text
        assert "[PANIC]" not in text and "[TEST] failed" not in text, text
        if module:
            expected = 2166136261
            for byte in module.read_bytes():
                expected = ((expected ^ byte) * 16777619) & 0xffffffff
            assert f"[TEST] module hash={expected:08x}" in text, text
    print(f"PASS demand paging, remapping, address spaces and frame exhaustion: {ram} MB"
          + (", boot module preserved" if module else ""), flush=True)


def fault(args, case, vector, error, address=None, ram=64):
    with VM(args, f"fault-{case}", ram, append=f"test={case}") as vm:
        vm.wait(lambda text: "[PANIC]" in text and "\n" in text.split("[PANIC]", 1)[1])
        text = vm.text()
        expected = f"[PANIC] exception={vector:08x} error={error:08x} eip="
        assert expected in text, text
        if address is not None:
            assert f"address={address:08x}" in text, text
        vm.wait(lambda text: "Kernel exception" in text)
        time.sleep(0.02)
        registers = vm.monitor("info registers")
        assert "HLT=1" in registers, registers
        flags = int(re.search(r"EFL=([0-9a-fA-F]+)", registers)[1], 16)
        assert not flags & 0x200, registers
        assert vm.text().count("[CPU] Exception frames verified") == 1
        assert "[TEST] passed" not in text and "[TEST] failed" not in text, text
    print(f"PASS fatal {case}: correct exception and halted CPU", flush=True)


def main():
    parser = argparse.ArgumentParser()
    image = parser.add_mutually_exclusive_group(required=True)
    image.add_argument("--kernel", type=Path)
    image.add_argument("--iso", type=Path)
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--results", type=Path, default=Path("integration_tests/results/paging"))
    args = parser.parse_args()
    if not shutil.which(args.qemu):
        parser.error(f"{args.qemu} is missing")
    args.results.mkdir(parents=True, exist_ok=True)
    if args.iso:
        boot(args, 64)
        boot(args, 128, "q35")
        return
    for ram in (16, 32, 64, 128, 512, 1536):
        boot(args, ram)
        paging(args, ram)
    boot(args, 64, "q35")
    with tempfile.TemporaryDirectory(prefix="kmos-module-") as temp:
        module = Path(temp) / "module.bin"
        module.write_bytes(bytes(range(256)) * 4096)
        paging(args, 64, module)
    for case, vector, error, address in (
        ("null", 14, 0, 0),
        ("readonly", 14, 3, 0xe0000000),
        ("user-readonly", 14, 3, 0x40000000),
        ("text", 14, 3, 0x101000),
        ("invalid-opcode", 6, 0, None),
        ("outside-heap", 14, 2, 0xe0600000),
        ("out-of-memory", 14, 2, 0xe0000000),
    ):
        fault(args, case, vector, error, address, ram=16 if case == "out-of-memory" else 64)


if __name__ == "__main__":
    main()
