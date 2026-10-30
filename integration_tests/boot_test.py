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
        count = len(re.findall(r"^kmOS:[^\r\n]*\$ ", self.text(), re.MULTILINE))
        self.process.stdin.write((command + "\n").encode())
        self.process.stdin.flush()
        return self.wait(lambda text: len(re.findall(r"^kmOS:[^\r\n]*\$ ", text, re.MULTILINE)) > count)

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
        if args.iso:
            assert all(value in vm.text() for value in ("Starting userland", "Hello from ring 3",
                                                      "kmOS userland", "[USER] Reaped init")), vm.text()
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
        for command in ("fetch", "lsb_release -a"):
            start = len(vm.text())
            info = vm.serial_command(command)[start:]
            for field in ("kmOS 1.0", "Kernel: monolithic", "Arch: x86 (32-bit)",
                          "CPU: ", "Memory: ", "Uptime: ", "Paging: on", "Built: "):
                assert field in info, info
            assert len(re.findall(r"^[ .+#]{32}", info, re.MULTILINE)) == 16, info
            assert all(len(line) < 80 for line in info.splitlines()), info
        start = len(vm.text())
        assert "Usage: lsb_release" in vm.serial_command("lsb_release -x")[start:]
        for command in ("help", "mem", "ps", "uptime"):
            text = vm.serial_command(command)
        ticks = int(re.findall(r"Timer ticks: (\d+)", text)[-1])
        time.sleep(0.15)
        text = vm.serial_command("uptime")
        assert int(re.findall(r"Timer ticks: (\d+)", text)[-1]) > ticks, text
        assert "kernel_main" in text and "idle" in text, text
        for command in ("ls /dev", "ls /mnt", "ls /sys"):
            text = vm.serial_command(command)
        navigation(vm)
        text = vm.text()
        assert "[PANIC]" not in text, text
        assert "Invalid magic" not in text and "Invalid block" not in text, text
    print(f"PASS boot, paging, keyboard, serial and timer: {machine}, {ram} MB", flush=True)


def navigation(vm):
    cases = (
        ("ls dev", "/", "Contents of dev:"),
        ("cd sys/env", "/sys/env", None),
        ("pwd", "/sys/env", "\n/sys/env\n"),
        ("ls ..", "/sys/env", "env/"),
        ("cd ../..", "/", None),
        ("cd ../../..", "/", None),
        ("cd //sys///env/./", "/sys/env", None),
        ("cd missing", "/sys/env", "No such file or directory"),
        ("cat missing", "/sys/env", "No such file or directory"),
        ("ls /missing/child", "/sys/env", "No such file or directory"),
        ("cd .", "/sys/env", None),
        ("cd /dev/../sys/env/../../dev", "/dev", None),
        ("pwd", "/dev", "\n/dev\n"),
        ("cd", "/", None),
        ("ls ///sys//env/../", "/", "env/"),
        ("cd " + "n" * 256, "/", "No such file or directory"),
    )
    for command, directory, expected in cases:
        start = len(vm.text())
        text = vm.serial_command(command)[start:]
        assert text.endswith(f"kmOS:{directory}$ "), text
        if expected:
            assert expected in text, text


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


def heap(args, ram):
    with VM(args, f"heap-{ram}", ram, append="test=heap") as vm:
        text = vm.wait(lambda text: "[TEST] passed" in text or "[TEST] failed" in text)
        assert vm.process.wait(timeout=2) == 33, text
        assert "[HEAP] allocation, reuse, realloc and exhaustion verified" in text, text
        assert "[PANIC]" not in text and "Invalid magic" not in text and "Invalid block" not in text, text
    print(f"PASS heap ownership, reuse, realloc and exhaustion: {ram} MB", flush=True)


def filesystem(args, ram):
    with VM(args, f"filesystem-{ram}", ram, append="test=filesystem") as vm:
        text = vm.wait(lambda text: "[TEST] passed" in text or "[TEST] failed" in text)
        assert vm.process.wait(timeout=2) == 33, text
        assert "[FS] paths, working directories and lifetimes verified" in text, text
        assert "filesystem read ok" in text, text
        assert "[PANIC]" not in text and "Invalid magic" not in text and "Invalid block" not in text, text
    print(f"PASS filesystem paths, working directories and lifetimes: {ram} MB", flush=True)


def cow(args, ram):
    with VM(args, f"cow-{ram}", ram, append="test=cow") as vm:
        text = vm.wait(lambda text: "[TEST] passed" in text or "[TEST] failed" in text)
        assert vm.process.wait(timeout=2) == 33, text
        assert "[COW] ownership, isolation, rollback and cleanup verified" in text, text
        assert "[PANIC]" not in text and "Invalid magic" not in text and "Invalid block" not in text, text
    print(f"PASS COW ownership, isolation, rollback and cleanup: {ram} MB", flush=True)


def process_files(args, ram):
    with VM(args, f"process-files-{ram}", ram, append="test=process") as vm:
        text = vm.wait(lambda text: "[TEST] passed" in text or "[TEST] failed" in text)
        assert vm.process.wait(timeout=2) == 33, text
        assert "[PROC] descriptor references, close, removal and cwd verified" in text, text
        assert "[PANIC]" not in text and "Invalid magic" not in text, text
    print(f"PASS descriptor ownership, close, removal and cwd: {ram} MB", flush=True)


def build_process_tests(args, temp):
    repo = Path(__file__).resolve().parents[1]
    for source, name in (("process_test.c", "init"), ("exec_test.c", "exec-target")):
        obj = temp / (name + ".o")
        subprocess.run([args.cross + "-gcc", "-std=c11", "-m32", "-O2", "-g", "-Wall", "-Wextra",
                        "-ffreestanding", "-fno-builtin", "-fno-pic", "-fno-pie", "-fno-stack-protector",
                        "-I", str(repo / "src/userland"), "-c", str(repo / "integration_tests" / source),
                        "-o", str(obj)], check=True)
        subprocess.run([args.cross + "-ld", "-m", "elf_i386", "-z", "noexecstack", "-T",
                        str(repo / "src/userland/linker.ld"), "-o", str(temp / (name + ".elf")),
                        str(repo / "src/userland/start.o"), str(obj)], check=True)
    return ",".join(str(path) for path in (temp / "init.elf", temp / "exec-target.elf",
                                          repo / "src/userland/hello.elf"))


def processes(args, module, ram, machine="pc"):
    with VM(args, f"processes-{machine}-{ram}", ram, machine, append="user", module=module) as vm:
        text = vm.wait(lambda text: "kmOS:/$ " in text, timeout=30)
        assert "process lifecycle verified" in text and "[USER] Exit status=00000000" in text, text
        assert "[USER] Reaped init" in text and "Frame leak" not in text, text
        assert "[PANIC]" not in text and "Invalid magic" not in text and "Invalid block" not in text, text
        assert "\nsurvived\n" in vm.serial_command("echo survived"), vm.text()
        text = vm.serial_command("ps")
        assert "init" not in text.split("ps\n")[-1] and "exec-target" not in text.split("ps\n")[-1], text
    print(f"PASS fork, exec, wait, descriptors, COW, faults, limits and orphans: {machine}, {ram} MB", flush=True)


def userland(args, ram=64):
    directory = Path(__file__).resolve().parents[1] / "src/userland"
    module = ",".join(str(directory / (name + ".elf")) for name in ("init", "hello", "cat"))
    with VM(args, f"userland-{ram}", ram, append="user", module=module) as vm:
        text = vm.wait(lambda text: "kmOS:/$ " in text)
        for expected in ("Starting userland", "Hello from ring 3", "kmOS userland",
                         "[USER] Exit status=00000000", "[USER] Reaped init"):
            assert expected in text, text
        assert "[PANIC]" not in text and "Frame leak" not in text, text
    print(f"PASS user init, hello and cat: {ram} MB", flush=True)


def elf(args, ram):
    with VM(args, f"elf-{ram}", ram, append="test=elf") as vm:
        text = vm.wait(lambda text: "[TEST] passed" in text or "[TEST] failed" in text)
        assert vm.process.wait(timeout=2) == 33, text
        assert "[ELF] validation, mappings, stack and rollback verified" in text, text
        assert "[PANIC]" not in text, text
    print(f"PASS ELF validation, mappings, stack and rollback: {ram} MB", flush=True)


def user(args, module, name="hello", status=0, ram=64, machine="pc", vector=None, rejected=False):
    with VM(args, f"user-{name}-{ram}", ram, machine, append="user", module=module) as vm:
        text = vm.wait(lambda text: "kmOS:/$ " in text)
        assert "[BOOT] Ready" in text and "[PANIC]" not in text, text
        assert "Frame leak" not in text and "Invalid magic" not in text and "Invalid block" not in text, text
        if rejected:
            assert "[USER] Rejected ELF" in text and "[USER] Started" not in text, text
        else:
            assert f"[USER] Exit status={status:08x}" in text and "[USER] Reaped init" in text, text
            if name == "hello":
                assert "Hello from ring 3" in text, text
            if vector is not None:
                assert f"[USER] fault vector={vector:08x}" in text, text
        text = vm.serial_command("echo survived")
        assert "\nsurvived\n" in text, text
        text = vm.serial_command("ps")
        assert "init" not in text.split("ps\n")[-1], text
        text = vm.serial_command("uptime")
        ticks = int(re.findall(r"Timer ticks: (\d+)", text)[-1])
        time.sleep(0.03)
        text = vm.serial_command("uptime")
        assert int(re.findall(r"Timer ticks: (\d+)", text)[-1]) > ticks, text
    print(f"PASS user {name}, kernel survives and reaps: {machine}, {ram} MB", flush=True)


def user_failures(args, temp):
    linker = Path(__file__).resolve().parents[1] / "src/userland/linker.ld"
    cases = (
        ("unknown-syscalls", """
            mov eax, 100
            int 0x80
            cmp eax, -38
            jne failed
            mov eax, -1
            int 0x80
            cmp eax, -38
            jne failed
            mov eax, 14
            int 0x80
            cmp eax, -38
            jne failed
            mov eax, 4
            mov ebx, 9
            xor ecx, ecx
            mov edx, 1
            int 0x80
            cmp eax, -9
            jne failed
            mov eax, 4
            mov ebx, 1
            xor ecx, ecx
            xor edx, edx
            int 0x80
            test eax, eax
            jnz failed
        """, 0, None),
        ("null-pointer", "mov ecx, 0\nmov edx, 1\njmp write", 142, None),
        ("kernel-pointer", "mov ecx, 0x100000\nmov edx, 1\njmp write", 142, None),
        ("unmapped-pointer", "mov ecx, 0x80000000\nmov edx, 1\njmp write", 142, None),
        ("overflow-pointer", "mov ecx, message\nmov edx, 0xffffffff\njmp write", 142, None),
        ("cross-page-pointer", "mov ecx, 0xdfffffff\nmov edx, 2\njmp write", 142, None),
        ("cross-gap-pointer", "mov ecx, 0x40001fff\nmov edx, 2\njmp write", 142, None),
        ("kernel-read", "mov eax, [0x100000]", 142, 14),
        ("kernel-write", "mov dword [0x100000], 0", 142, 14),
        ("readonly-text", "mov byte [_start], 0", 142, 14),
        ("stack-guard", "mov dword [0xdfffbffc], 0", 142, 14),
        ("unmapped-read", "mov eax, [0x80000000]", 142, 14),
        ("invalid-opcode", "ud2", 134, 6),
        ("privileged-cli", "cli", 141, 13),
        ("privileged-io", "mov dx, 0x3f8\nout dx, al", 141, 13),
        ("privileged-gate", "int 0x20", 141, 13),
        ("exit-code", "mov ebx, 42\njmp exit", 42, None),
        ("writes", """
            mov ecx, 0xdfffefff
            mov byte [ecx], 'A'
            mov byte [ecx + 1], 10
            mov eax, 4
            mov ebx, 1
            mov edx, 2
            int 0x80
            cmp eax, 2
            jne failed
            mov eax, 4
            mov ebx, 2
            mov ecx, message
            mov edx, 10
            int 0x80
            cmp eax, 10
            jne failed
            mov eax, 4
            mov ebx, 1
            mov ecx, 0xdfffc000
            mov edx, 8192
            int 0x80
            cmp eax, 4096
            jne failed
        """, 0, None),
        ("registers", """
            xor eax, eax
            mov fs, ax
            mov gs, ax
            mov ebx, 0x12345678
            mov edx, 0x23456789
            mov esi, 0x3456789a
            mov edi, 0x456789ab
            mov ebp, 0x56789abc
            mov ecx, 200000000
            std
        spin:
            dec ecx
            jnz spin
            pushfd
            pop eax
            test eax, 0x400
            jz failed
            cld
            mov ax, fs
            test ax, ax
            jnz failed
            mov ax, gs
            test ax, ax
            jnz failed
            mov eax, 0xffffffff
            int 0x80
            cmp eax, -38
            jne failed
            cmp ebx, 0x12345678
            jne failed
            cmp edx, 0x23456789
            jne failed
            cmp esi, 0x3456789a
            jne failed
            cmp edi, 0x456789ab
            jne failed
            cmp ebp, 0x56789abc
            jne failed
        """, 0, None),
    )
    template = """bits 32
section .text
global _start
_start:
{body}
    xor ebx, ebx
    jmp exit
failed:
    mov ebx, 1
exit:
    mov eax, 1
    int 0x80
    ud2
write:
    mov eax, 4
    mov ebx, 1
    int 0x80
    jmp failed
section .rodata
message: db 'user test', 10
section .data
    dd 0
section .note.GNU-stack noalloc noexec nowrite progbits
"""
    for name, body, status, vector in cases:
        source = temp / f"{name}.asm"
        obj = source.with_suffix(".o")
        module = source.with_suffix(".elf")
        source.write_text(template.format(body=body))
        subprocess.run(["nasm", "-f", "elf32", str(source), "-o", str(obj)], check=True)
        subprocess.run([args.cross + "-ld", "-m", "elf_i386", "-z", "noexecstack", "-T",
                        str(linker), "-o", str(module), str(obj)], check=True)
        user(args, module, name, status, vector=vector)
    bad = temp / "malformed.elf"
    bad.write_bytes(b"\x7fELF")
    user(args, bad, "malformed", rejected=True)
    hello = linker.parent / "hello.elf"
    modules = ",".join(str(path) for path in (hello, bad, temp / "invalid-opcode.elf", hello))
    with VM(args, "user-multiple", append="user", module=modules) as vm:
        text = vm.wait(lambda text: "kmOS:/$ " in text)
        assert text.count("Hello from ring 3") == 2, text
        assert text.count("[USER] Reaped init") == 3 and "[USER] Rejected ELF" in text, text
        assert "[USER] Exit status=00000086" in text, text
        assert "[PANIC]" not in text and "Frame leak" not in text, text
        assert "\nsurvived\n" in vm.serial_command("echo survived"), vm.text()
    print("PASS successive user programs after rejection and faults", flush=True)


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
    parser.add_argument("--cross", default="i686-elf")
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
        heap(args, ram)
        filesystem(args, ram)
        cow(args, ram)
        elf(args, ram)
        process_files(args, ram)
        user(args, Path(__file__).resolve().parents[1] / "src/userland/hello.elf", ram=ram)
    with tempfile.TemporaryDirectory(prefix="kmos-process-") as temp:
        module = build_process_tests(args, Path(temp))
        for ram in (16, 32, 64, 128, 512, 1536):
            processes(args, module, ram)
        processes(args, module, 64, "q35")
    userland(args)
    boot(args, 64, "q35")
    user(args, Path(__file__).resolve().parents[1] / "src/userland/hello.elf", machine="q35")
    with tempfile.TemporaryDirectory(prefix="kmos-user-") as temp:
        user_failures(args, Path(temp))
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
