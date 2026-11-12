#!/usr/bin/env python3
import argparse
import json
from pathlib import Path
import re
import shutil
import socket
import struct
import subprocess
import tempfile
import time


class VM:
    def __init__(self, args, name, ram=64, machine="pc", append="", module=None, disk=None, cpu=None,
                 swap_disk=None, disk_write_bps=None):
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
        if disk:
            drive = f"file={disk},format=raw,if=ide,index=0"
            if disk_write_bps:
                drive += f",bps_wr={disk_write_bps}"
            command += ["-drive", drive]
        if swap_disk:
            command += ["-drive", f"file={swap_disk},format=raw,if=ide,index=1"]
        if cpu:
            command += ["-cpu", cpu]
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

    def send_serial(self, data):
        self.process.stdin.write(data)
        self.process.stdin.flush()

    def serial_command(self, command):
        count = len(re.findall(r"^kmOS:[^\r\n]*\$ ", self.text(), re.MULTILINE))
        self.send_serial((command + "\n").encode())
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
    version = getattr(args, "version", None)
    revision = None
    if not version:
        metadata = Path(__file__).resolve().parents[1] / "src/kernel/generated/version.json"
        identity = json.loads(metadata.read_text())
        version = identity["display_version"]
        revision = identity["short_revision"]
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
            for field in (f"kmOS {version}", "Kernel: monolithic", "Arch: x86 (32-bit)",
                          "CPU: ", "Memory: ", "Uptime: ", "Paging: on", "Built: ",
                          f"Commit: {revision}" if revision else "Commit: "):
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


def build_user_test(args, temp, source, name):
    repo = Path(__file__).resolve().parents[1]
    obj = temp / (name + ".o")
    program = temp / (name + ".elf")
    subprocess.run([args.cross + "-gcc", "-std=c11", "-m32", "-O2", "-g", "-Wall", "-Wextra",
                    "-ffreestanding", "-fno-builtin", "-fno-pic", "-fno-pie", "-fno-stack-protector",
                    "-I", str(repo / "src/userland"), "-c", str(repo / "integration_tests" / source),
                    "-o", str(obj)], check=True)
    subprocess.run([args.cross + "-ld", "-m", "elf_i386", "-z", "noexecstack", "-T",
                    str(repo / "src/userland/linker.ld"), "-o", str(program),
                    str(repo / "src/userland/start.o"), str(repo / "src/userland/alloc.o"), str(obj)], check=True)
    return program


def build_process_tests(args, temp):
    repo = Path(__file__).resolve().parents[1]
    for source, name in (("process_test.c", "init"), ("exec_test.c", "exec-target")):
        build_user_test(args, temp, source, name)
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


def storage(args, ram):
    with VM(args, f"storage-{ram}", ram, append="test=storage") as vm:
        text = vm.wait(lambda text: "[TEST] passed" in text or "[TEST] failed" in text)
        assert vm.process.wait(timeout=2) == 33, text
        assert "[STORAGE] ext2 geometry, records, reads and errors verified" in text, text
        assert "[PANIC]" not in text, text
    print(f"PASS ext2 valid images, malformed metadata and I/O errors: {ram} MB", flush=True)


def user_signals(args, temp):
    program = build_user_test(args, temp, "signal_test.c", "init")
    with VM(args, "signal-handlers", append="user", module=program) as vm:
        text = vm.wait(lambda text: "running signal ready" in text or "signal test failed" in text)
        assert "signal test failed" not in text, text
        vm.send_serial(b"\x03")
        for count in range(1, 7):
            text = vm.wait(lambda text: text.count("blocked signal ready") >= count or "signal test failed" in text)
            assert "signal test failed" not in text, text
            time.sleep(0.05)
            vm.send_serial(b"x")
        text = vm.wait(lambda text: "kmOS:/$ " in text or "[PANIC]" in text)
        assert "signal handlers and syscall restart verified" in text and "[USER] Exit status=00000000" in text, text
        assert "[USER] Reaped init" in text and "[PANIC]" not in text and "Frame leak" not in text, text
    print("PASS signal handlers, inheritance, interrupted and restarted syscalls", flush=True)


def user_shell(args, temp):
    program = build_user_test(args, temp, "pipe_test.c", "init")
    with VM(args, "pipe-signal", append="user", module=program) as vm:
        text = vm.wait(lambda text: "kmOS:/$ " in text or "[PANIC]" in text)
        assert "pipe and signal semantics verified" in text and "[USER] Exit status=00000000" in text, text
        assert "[USER] Reaped init" in text and "[PANIC]" not in text and "Frame leak" not in text, text
    print("PASS blocking pipes, descriptor rollback, SIGPIPE and process-group signals", flush=True)

    userland = Path(__file__).resolve().parents[1] / "src/userland"
    modules = ",".join(str(userland / (name + ".elf")) for name in ("init", "hello", "cat", "sh"))
    with VM(args, "user-shell", append="user usershell", module=modules) as vm:
        vm.wait(lambda text: "sh$ " in text)
        for command, output in (
            ("echo 'hello | world' | cat|cat", "\nhello | world\n"),
            ("cd /etc", ""),
            ("cat motd", "\nkmOS userland\n"),
            ("echo oops |", "sh: syntax error"),
        ):
            prompts = vm.text().count("sh$ ")
            vm.send_serial((command + "\n").encode())
            text = vm.wait(lambda text: text.count("sh$ ") > prompts)
            assert output in text, text
        start = len(vm.text())
        vm.send_serial(b"cat | cat\n")
        vm.wait(lambda text: "cat | cat\n" in text[start:])
        vm.send_serial(b"foreground\n")
        vm.wait(lambda text: "\nforeground\n" in text[start:])
        vm.send_serial(b"\x03")
        vm.wait(lambda text: "^C\nsh$ " in text[start:])
        vm.send_serial(b"exit\n")
        text = vm.wait(lambda text: "kmOS:/$ " in text)
        assert "[USER] Reaped sh" in text and "Frame leak" not in text and "[PANIC]" not in text, text
    print("PASS interactive userspace shell, pipelines and Ctrl-C", flush=True)


def user_heap(args, temp):
    program = build_user_test(args, temp, "user_heap_test.c", "init")
    with VM(args, "user-heap", 32, append="user", module=program) as vm:
        text = vm.wait(lambda text: "kmOS:/$ " in text or "[PANIC]" in text)
        assert "userspace heap verified" in text and "[USER] Exit status=00000000" in text, text
        assert "[USER] Reaped init" in text and "[PANIC]" not in text and "Frame leak" not in text, text
    print("PASS userspace heap growth, shrink, exhaustion and fork", flush=True)


def disk_mount(args, temp):
    disk = temp / "ext2.img"
    block = 1024
    image = bytearray(16 * block)
    struct.pack_into("<11I", image, block, 16, 16, 0, 6, 5, 1, 0, 0, 16, 16, 16)
    struct.pack_into("<HH", image, block + 56, 0xef53, 1)
    struct.pack_into("<I", image, block + 76, 1)
    struct.pack_into("<IHHI", image, block + 84, 11, 128, 0, 0)
    struct.pack_into("<I", image, block + 96, 2)
    struct.pack_into("<IIIHHH", image, 2 * block, 3, 4, 5, 6, 5, 1)
    image[3 * block:4 * block] = b"\xff\x81" + b"\xff" * (block - 2)
    image[4 * block:5 * block] = b"\xff\x07" + b"\xff" * (block - 2)
    for inode, mode, size, links, blocks in (
        (2, 0o40755, block, 2, [7]),
        (11, 0o100644, 1300, 1, [8, 9]),
    ):
        start = 5 * block + (inode - 1) * 128
        struct.pack_into("<HHIIIIIHHI", image, start, mode, 0, size, 0, 0, 0, 0, 0, links, 2 * len(blocks))
        struct.pack_into(f"<{len(blocks)}I", image, start + 40, *blocks)
    for offset, inode, length, kind, name in (
        (0, 2, 12, 2, b"."), (12, 2, 12, 2, b".."), (24, 11, block - 24, 1, b"note"),
    ):
        position = 7 * block + offset
        struct.pack_into("<IHBB", image, position, inode, length, len(name), kind)
        image[position + 8:position + 8 + len(name)] = name
    image[8 * block:8 * block + 1300] = b"Z" * 1300
    disk.write_bytes(image)

    program = build_user_test(args, temp, "disk_test.c", "init")
    for mode, argument in (("rw", "disk=hda,rw"), ("ro", "disk=hda")):
        with VM(args, f"disk-{mode}", append=f"user {argument}", module=program, disk=disk) as vm:
            text = vm.wait(lambda text: "kmOS:/$ " in text or "[PANIC]" in text)
            assert f"[DISK] /mnt/disk mounted {mode}" in text and "disk contents verified" in text, text
            assert "[USER] Exit status=00000000" in text and "[PANIC]" not in text, text
        image[8 * block:8 * block + 1028] = bytes(33 + i % 90 for i in range(1028))
        assert disk.read_bytes() == image, f"unexpected disk contents after {mode} boot"
    print("PASS ext2 ATA mount, cross-block writes and read-only reboot", flush=True)

    malformed = bytearray(image)
    struct.pack_into("<I", malformed, 5 * block + 10 * 128 + 44, 3)
    disk.write_bytes(malformed)
    with VM(args, "disk-metadata", append="user disk=hda,rw", module=program, disk=disk) as vm:
        text = vm.wait(lambda text: "kmOS:/$ " in text or "[PANIC]" in text)
        assert "disk write rejected" in text and "[USER] Exit status=00000000" in text, text
        assert "[PANIC]" not in text, text
    assert disk.read_bytes() == malformed
    print("PASS ext2 rejects writes into metadata before changing data", flush=True)

    disk.write_bytes(image[:9 * block])
    with VM(args, "disk-truncated", append="disk=hda,rw", disk=disk) as vm:
        text = vm.wait(lambda text: "[BOOT] Ready" in text)
        assert "[DISK] Could not mount hda" in text and "[PANIC]" not in text, text
    print("PASS boot with truncated ext2 disk", flush=True)


def disk_growth(args, temp):
    with VM(args, "ext2-write-errors", append="test=storage-write") as vm:
        text = vm.wait(lambda text: "[TEST] passed" in text or "[TEST] failed" in text)
        assert vm.process.wait(timeout=2) == 33, text
        assert "[STORAGE] Allocation rollback and write failures verified" in text, text
    print("PASS ext2 allocation rollback and failures during creation and growth", flush=True)

    for tool in ("mke2fs", "e2fsck"):
        assert shutil.which(tool), f"{tool} is missing; install e2fsprogs"
    program = build_user_test(args, temp, "disk_growth_test.c", "init")
    for name, block_size, options in (
        ("1024", 1024, ["-g", "256", "-O", "^resize_inode"]),
        ("2048", 2048, ["-g", "256", "-O", "^resize_inode"]),
        ("4096", 4096, ["-g", "256", "-O", "^resize_inode"]),
        ("default", 1024, []),
        ("revision-0", 1024, ["-I", "128", "-O", "none"]),
    ):
        disk = temp / f"ext2-growth-{name}.img"
        with disk.open("wb") as image:
            image.truncate(8 * 1024 * 1024)
        result = subprocess.run(["mke2fs", "-q", "-t", "ext2", "-F", "-b", str(block_size),
                                 "-N", "256", *options, str(disk)],
                                capture_output=True, text=True)
        assert result.returncode == 0, result.stdout + result.stderr
        if name == "revision-0":
            with disk.open("r+b") as image:
                image.seek(1024 + 76)
                image.write(struct.pack("<I", 0))
                image.seek(1024 + 84)
                image.write(bytes(1024 - 84))
        for mode, marker in (("rw", "file creation, growth and sparse writes verified"),
                             ("ro", "created files verified after reboot")):
            argument = "disk=hda,rw" if mode == "rw" else "disk=hda"
            with VM(args, f"ext2-growth-{name}-{mode}", append=f"user {argument}",
                    module=program, disk=disk) as vm:
                text = vm.wait(lambda text: "kmOS:/$ " in text or "[PANIC]" in text, timeout=30)
                assert marker in text and "[USER] Exit status=00000000" in text, text
                assert "[USER] Reaped init" in text and "Frame leak" not in text and "[PANIC]" not in text, text
            checked = subprocess.run(["e2fsck", "-f", "-n", str(disk)], capture_output=True, text=True)
            assert checked.returncode == 0, checked.stdout + checked.stderr
        print(f"PASS ext2 creation, growth, sparse writes, reboot and fsck: {name}", flush=True)

    for name, size, source, marker, counter in (
        ("blocks", 64 * 1024, "disk_full_test.c", "full disk data verified", 12),
        ("inodes", 1024 * 1024, "disk_inode_test.c", "inode exhaustion verified", 16),
    ):
        disk = temp / f"ext2-full-{name}.img"
        with disk.open("wb") as image:
            image.truncate(size)
        made = subprocess.run(["mke2fs", "-q", "-t", "ext2", "-F", "-b", "1024",
                               "-N", "32", "-O", "^resize_inode", str(disk)], capture_output=True, text=True)
        assert made.returncode == 0, made.stdout + made.stderr
        program = build_user_test(args, temp, source, "init")
        for mode in ("rw", "ro"):
            argument = "disk=hda,rw" if mode == "rw" else "disk=hda"
            with VM(args, f"ext2-full-{name}-{mode}", append=f"user {argument}", module=program, disk=disk) as vm:
                text = vm.wait(lambda text: "kmOS:/$ " in text or "[PANIC]" in text, timeout=30)
                assert marker in text and "[USER] Exit status=00000000" in text, text
                assert "[USER] Reaped init" in text and "[PANIC]" not in text, text
            checked = subprocess.run(["e2fsck", "-f", "-n", str(disk)], capture_output=True, text=True)
            assert checked.returncode == 0, checked.stdout + checked.stderr
            with disk.open("rb") as image:
                image.seek(1024 + counter)
                assert struct.unpack("<I", image.read(4))[0] == 0
        print(f"PASS ext2 exhaustion, reboot and fsck: {name}", flush=True)


def disk_remove(args, temp):
    with VM(args, "ext2-remove-errors", append="test=storage-remove") as vm:
        text = vm.wait(lambda text: "[TEST] passed" in text or "[TEST] failed" in text)
        assert vm.process.wait(timeout=2) == 33, text
        assert "[STORAGE] Truncate, unlink and close failures verified" in text, text
    print("PASS failures during truncation, unlink and final close", flush=True)

    for tool in ("mke2fs", "e2fsck", "debugfs"):
        assert shutil.which(tool), f"{tool} is missing; install e2fsprogs"
    program = build_user_test(args, temp, "disk_remove_test.c", "init")
    payload = temp / "payload"
    payload.write_bytes(b"hard")
    for name, block_size, options in (
        ("1024", 1024, ["-O", "^resize_inode"]),
        ("2048", 2048, ["-O", "^resize_inode"]),
        ("4096", 4096, ["-O", "^resize_inode"]),
        ("revision-0", 1024, ["-O", "none"]),
    ):
        disk = temp / f"ext2-remove-{name}.img"
        with disk.open("wb") as image:
            image.truncate(8 * 1024 * 1024)
        made = subprocess.run(["mke2fs", "-q", "-t", "ext2", "-F", "-b", str(block_size),
                               "-I", "128", *options, str(disk)], capture_output=True, text=True)
        assert made.returncode == 0, made.stdout + made.stderr
        if name == "revision-0":
            with disk.open("r+b") as image:
                image.seek(1024 + 76)
                image.write(struct.pack("<I", 0))
                image.seek(1024 + 84)
                image.write(bytes(1024 - 84))
        commands = temp / "debugfs-commands"
        text = f"write {payload} /hard-a\nlink /hard-a /hard-b\nset_inode_field /hard-a links_count 2\n"
        if name != "revision-0":
            text += f"write {payload} /attr-a\nwrite {payload} /attr-b\nea_set /attr-a user.test value\n"
        commands.write_text(text)
        seeded = subprocess.run(["debugfs", "-w", "-f", str(commands), str(disk)], capture_output=True, text=True)
        assert seeded.returncode == 0, seeded.stdout + seeded.stderr
        if name != "revision-0":
            first = subprocess.run(["debugfs", "-R", "stat /attr-a", str(disk)], capture_output=True, text=True)
            second = subprocess.run(["debugfs", "-R", "stat /attr-b", str(disk)], capture_output=True, text=True)
            acl = int(re.search(r"File ACL: (\d+)", first.stdout)[1])
            blocks = int(re.search(r"Blockcount: (\d+)", second.stdout)[1])
            assert acl, first.stdout
            commands.write_text(f"set_inode_field /attr-b file_acl {acl}\n"
                                f"set_inode_field /attr-b blocks {blocks + block_size // 512}\n")
            seeded = subprocess.run(["debugfs", "-w", "-f", str(commands), str(disk)], capture_output=True, text=True)
            assert seeded.returncode == 0, seeded.stdout + seeded.stderr
            with disk.open("r+b") as image:
                image.seek(acl * block_size + 4)
                image.write(struct.pack("<I", 2))
        checked = subprocess.run(["e2fsck", "-f", "-n", str(disk)], capture_output=True, text=True)
        assert checked.returncode == 0, checked.stdout + checked.stderr
        for mode, marker in (("rw", "truncation, unlink and final close verified"),
                             ("ro", "truncation and removal verified after reboot")):
            argument = "disk=hda,rw" if mode == "rw" else "disk=hda"
            with VM(args, f"ext2-remove-{name}-{mode}", append=f"user {argument}", module=program, disk=disk) as vm:
                text = vm.wait(lambda text: "kmOS:/$ " in text or "[PANIC]" in text, timeout=30)
                assert marker in text and "[USER] Exit status=00000000" in text, text
                assert "[USER] Reaped init" in text and "Frame leak" not in text and "[PANIC]" not in text, text
            checked = subprocess.run(["e2fsck", "-f", "-n", str(disk)], capture_output=True, text=True)
            assert checked.returncode == 0, checked.stdout + checked.stderr
        print(f"PASS ext2 truncation, unlink, hard links, shared attributes and final close: {name}", flush=True)

    disk = temp / "ext2-orphan-crash.img"
    with disk.open("wb") as image:
        image.truncate(8 * 1024 * 1024)
    made = subprocess.run(["mke2fs", "-q", "-t", "ext2", "-F", "-O", "^resize_inode", str(disk)],
                          capture_output=True, text=True)
    assert made.returncode == 0, made.stdout + made.stderr
    with disk.open("rb") as image:
        image.seek(1024 + 12)
        free_before = image.read(8)
    orphan_program = build_user_test(args, temp, "disk_orphan_test.c", "init")
    with VM(args, "ext2-orphan-crash", append="user disk=hda,rw", module=orphan_program, disk=disk) as vm:
        text = vm.wait(lambda text: "open orphans ready" in text or "[PANIC]" in text)
        assert "open orphans ready" in text and "[PANIC]" not in text, text
    with VM(args, "ext2-orphan-reject", append="disk=hda,rw", disk=disk) as vm:
        text = vm.wait(lambda text: "[BOOT] Ready" in text)
        assert "[DISK] Could not mount hda" in text, text
    recovered = subprocess.run(["e2fsck", "-f", "-y", str(disk)], capture_output=True, text=True)
    assert recovered.returncode in (0, 1), recovered.stdout + recovered.stderr
    checked = subprocess.run(["e2fsck", "-f", "-n", str(disk)], capture_output=True, text=True)
    assert checked.returncode == 0, checked.stdout + checked.stderr
    with disk.open("rb") as image:
        image.seek(1024 + 12)
        assert image.read(8) == free_before, "orphan recovery leaked blocks or inodes"
    with VM(args, "ext2-orphan-recovered", append="disk=hda,rw", disk=disk) as vm:
        text = vm.wait(lambda text: "[BOOT] Ready" in text)
        assert "[DISK] /mnt/disk mounted rw" in text, text
    print("PASS recovery of open unlinked files after interrupted execution", flush=True)


def disk_directories(args, temp):
    with VM(args, "ext2-directory-errors", append="test=storage-directories") as vm:
        text = vm.wait(lambda text: "[TEST] passed" in text or "[TEST] failed" in text)
        assert vm.process.wait(timeout=2) == 33, text
        assert "[STORAGE] Directory allocation, validation and write failures verified" in text, text
    print("PASS directory allocation limits, malformed entries and write failures", flush=True)

    program = build_user_test(args, temp, "disk_directory_test.c", "init")
    for name, block_size, options in (
        ("1024", 1024, ["-g", "256", "-O", "^resize_inode"]),
        ("2048", 2048, ["-g", "256", "-O", "^resize_inode"]),
        ("4096", 4096, ["-g", "256", "-O", "^resize_inode"]),
        ("revision-0", 1024, ["-O", "none"]),
    ):
        disk = temp / f"ext2-directories-{name}.img"
        with disk.open("wb") as image:
            image.truncate(8 * 1024 * 1024)
        made = subprocess.run(["mke2fs", "-q", "-t", "ext2", "-F", "-b", str(block_size),
                               "-I", "128", *options, str(disk)], capture_output=True, text=True)
        assert made.returncode == 0, made.stdout + made.stderr
        if name == "revision-0":
            with disk.open("r+b") as image:
                image.seek(1024 + 76)
                image.write(struct.pack("<I", 0))
                image.seek(1024 + 84)
                image.write(bytes(1024 - 84))
        commands = temp / "directory-fixture"
        commands.write_text("mkdir /has-link\nsymlink /has-link/unknown target\n")
        seeded = subprocess.run(["debugfs", "-w", "-f", str(commands), str(disk)], capture_output=True, text=True)
        assert seeded.returncode == 0, seeded.stdout + seeded.stderr
        for mode, marker in (("rw", "mkdir, rmdir and removed working directories verified"),
                             ("ro", "directories verified after reboot")):
            argument = "disk=hda,rw" if mode == "rw" else "disk=hda"
            with VM(args, f"ext2-directories-{name}-{mode}", append=f"user {argument}", module=program, disk=disk) as vm:
                text = vm.wait(lambda text: "kmOS:/$ " in text or "[PANIC]" in text, timeout=45)
                assert marker in text and "[USER] Exit status=00000000" in text, text
                assert "[USER] Reaped init" in text and "Frame leak" not in text and "[PANIC]" not in text, text
            checked = subprocess.run(["e2fsck", "-f", "-n", str(disk)], capture_output=True, text=True)
            assert checked.returncode == 0, checked.stdout + checked.stderr
        print(f"PASS ext2 mkdir, rmdir, nonempty checks and removed working directories: {name}", flush=True)

    disk = temp / "ext2-directory-orphans.img"
    with disk.open("wb") as image:
        image.truncate(8 * 1024 * 1024)
    made = subprocess.run(["mke2fs", "-q", "-t", "ext2", "-F", "-O", "^resize_inode", str(disk)],
                          capture_output=True, text=True)
    assert made.returncode == 0, made.stdout + made.stderr
    with disk.open("rb") as image:
        image.seek(1024 + 12)
        free_before = image.read(8)
    orphan_program = build_user_test(args, temp, "disk_directory_orphan_test.c", "init")
    with VM(args, "ext2-directory-orphans", append="user disk=hda,rw", module=orphan_program, disk=disk) as vm:
        text = vm.wait(lambda text: "directory orphans ready" in text or "[PANIC]" in text)
        assert "directory orphans ready" in text and "[PANIC]" not in text, text
    with VM(args, "ext2-directory-orphans-reject", append="disk=hda,rw", disk=disk) as vm:
        text = vm.wait(lambda text: "[BOOT] Ready" in text)
        assert "[DISK] Could not mount hda" in text, text
    recovered = subprocess.run(["e2fsck", "-f", "-y", str(disk)], capture_output=True, text=True)
    assert recovered.returncode in (0, 1), recovered.stdout + recovered.stderr
    checked = subprocess.run(["e2fsck", "-f", "-n", str(disk)], capture_output=True, text=True)
    assert checked.returncode == 0, checked.stdout + checked.stderr
    with disk.open("rb") as image:
        image.seek(1024 + 12)
        assert image.read(8) == free_before, "directory recovery leaked blocks or inodes"
    print("PASS recovery of removed working directories after interrupted execution", flush=True)


def disk_indexed_directories(args, temp):
    disk = temp / "ext2-indexed-directories.img"
    with disk.open("wb") as image:
        image.truncate(8 * 1024 * 1024)
    made = subprocess.run(["mke2fs", "-q", "-t", "ext2", "-F", "-b", "1024", "-I", "128",
                           "-O", "dir_index,^resize_inode", str(disk)], capture_output=True, text=True)
    assert made.returncode == 0, made.stdout + made.stderr
    directories = ("create", "mkdir", "unlink", "rmdir", "keep")
    commands = temp / "indexed-directory-fixture"
    payload = temp / "indexed-directory-payload"
    payload.write_bytes(b"s")
    seed = [f"mkdir /{name}" for name in directories]
    seed.extend(f"write /dev/null /{name}/entry-{i:03d}-long-enough-for-multiple-directory-blocks"
                for name in directories for i in range(96))
    seed.extend([f"write {payload} /create/original", "write /dev/null /unlink/old",
                 "mkdir /rmdir/empty", "mkdir /keep/busy", "write /dev/null /keep/busy/file"])
    commands.write_text("\n".join(seed) + "\n")
    seeded = subprocess.run(["debugfs", "-w", "-f", str(commands), str(disk)], capture_output=True, text=True)
    assert seeded.returncode == 0, seeded.stdout + seeded.stderr
    optimized = subprocess.run(["e2fsck", "-f", "-y", "-D", str(disk)], capture_output=True, text=True)
    assert optimized.returncode in (0, 1), optimized.stdout + optimized.stderr

    def indexed(name):
        report = subprocess.run(["debugfs", "-R", f"stat /{name}", str(disk)], capture_output=True, text=True)
        flags = re.search(r"Flags: (0x[0-9a-fA-F]+)", report.stdout)
        assert report.returncode == 0 and flags, report.stdout + report.stderr
        return bool(int(flags.group(1), 16) & 0x1000)

    assert all(indexed(name) for name in directories)
    program = build_user_test(args, temp, "disk_indexed_directory_test.c", "init")
    for mode, marker in (("rw", "indexed ext2 directories modified"),
                         ("ro", "indexed ext2 directory changes verified after reboot")):
        argument = "disk=hda,rw" if mode == "rw" else "disk=hda"
        with VM(args, f"ext2-indexed-directories-{mode}", append=f"user {argument}", module=program, disk=disk) as vm:
            text = vm.wait(lambda text: "kmOS:/$ " in text or "[PANIC]" in text)
            assert marker in text and "[USER] Exit status=00000000" in text, text
            assert "[USER] Reaped init" in text and "Frame leak" not in text and "[PANIC]" not in text, text
        assert [name for name in directories if indexed(name)] == ["keep"]
        checked = subprocess.run(["e2fsck", "-f", "-n", str(disk)], capture_output=True, text=True)
        assert checked.returncode == 0, checked.stdout + checked.stderr
    print("PASS ext2 changes to indexed directories and read-only reboot", flush=True)


def swap(args, temp):
    with VM(args, "swap-slots", append="test=swap") as vm:
        text = vm.wait(lambda text: "[TEST] passed" in text or "[TEST] failed" in text)
        assert vm.process.wait(timeout=2) == 33, text
        assert "[SWAP] Slot ownership, protection and I/O errors verified" in text, text
    print("PASS swap slots, fork, permissions and I/O failures", flush=True)

    disk = temp / "swap.img"
    header = b"KMOS-SWAP\x00".ljust(4096, b"\x00")
    with disk.open("wb") as image:
        image.write(header)
        image.truncate(16 * 1024 * 1024)
    program = build_user_test(args, temp, "swap_test.c", "init")
    with VM(args, "swap-pressure", 16, append="user swap=hdb", module=program, swap_disk=disk) as vm:
        text = vm.wait(lambda text: "kmOS:/$ " in text or "[PANIC]" in text, timeout=60)
        assert "[SWAP] hdb enabled" in text and "swap under memory pressure verified" in text, text
        assert "[USER] Exit status=00000000" in text and "Frame leak" not in text, text
        assert "[PANIC]" not in text and "[USER] fault" not in text, text
        text = vm.serial_command("swap")
        assert "Used Swap:        0 MB (0 KB)" in text, text
    with disk.open("rb") as image:
        assert image.read(4096) == header and any(image.read(16 * 4096))
    print("PASS disk swap under user memory pressure", flush=True)

    with disk.open("r+b") as image:
        image.write(b"INVALID")
    with VM(args, "swap-invalid", append="swap=hdb", swap_disk=disk) as vm:
        text = vm.wait(lambda text: "[BOOT] Ready" in text)
        assert "[SWAP] hdb unavailable or invalid" in text and "[PANIC]" not in text, text
    print("PASS boot with invalid swap image", flush=True)


def storage_and_user_regressions(args, temp):
    disk = temp / "ata.img"
    contents = bytearray(16 * 512)
    contents[:512] = bytes([0x5a]) * 512
    contents[-512:] = bytes([0x7b]) * 512
    disk.write_bytes(contents)
    with VM(args, "ata", append="test=ata", disk=disk, disk_write_bps=2048) as vm:
        text = vm.wait(lambda text: "[TEST] passed" in text or "[TEST] failed" in text)
        assert vm.process.wait(timeout=2) == 33, text
        assert "[STORAGE] ATA bounds, transfers and flush verified" in text, text
        assert "[PANIC]" not in text, text
    contents[512:1536] = bytes((i * 17 + 3) & 255 for i in range(1024))
    assert disk.read_bytes() == contents, "ATA wrote outside the requested sectors"
    print("PASS ATA address bounds, slow writes and persisted contents", flush=True)

    read_program = build_user_test(args, temp, "read_test.c", "read-test")
    fpu_program = build_user_test(args, temp, "fpu_test.c", "init")
    fpu_exec = temp / "fpu-exec.elf"
    shutil.copyfile(fpu_program, fpu_exec)
    for name, ram, module, marker, cpu in (
        ("read-32", 32, read_program, "large read and COW verified", None),
        ("read-64", 64, read_program, "large read and COW verified", None),
        ("fpu-16", 16, f"{fpu_program},{fpu_exec}", "floating-point state verified", None),
        ("fpu-64", 64, f"{fpu_program},{fpu_exec}", "floating-point state verified", None),
        ("fpu-legacy", 64, f"{fpu_program},{fpu_exec}", "floating-point state verified",
         "qemu32,-fxsr,-sse,-sse2"),
        ("no-fpu", 64, Path(__file__).resolve().parents[1] / "src/userland/hello.elf",
         "Hello from ring 3", "qemu32,-fpu"),
    ):
        with VM(args, name, ram, append="user", module=module, cpu=cpu) as vm:
            text = vm.wait(lambda text: "[USER] Reaped init" in text or "[PANIC]" in text, timeout=30)
            assert marker in text and "[USER] Exit status=00000000" in text, text
            assert "[USER] Reaped init" in text and "[PANIC]" not in text and "Frame leak" not in text, text
        print(f"PASS {name}: isolation, cleanup and kernel survival", flush=True)


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
    parser.add_argument("--version", help="expected version for an externally built image")
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
        storage(args, ram)
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
    with tempfile.TemporaryDirectory(prefix="kmos-regressions-") as temp:
        storage_and_user_regressions(args, Path(temp))
        swap(args, Path(temp))
        disk_mount(args, Path(temp))
        disk_growth(args, Path(temp))
        disk_remove(args, Path(temp))
        disk_directories(args, Path(temp))
        disk_indexed_directories(args, Path(temp))
        user_heap(args, Path(temp))
        user_signals(args, Path(temp))
        user_shell(args, Path(temp))
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
