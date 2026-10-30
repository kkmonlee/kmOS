<p align="center">
  <img src="media/kmos.png" width="600" alt="kmOS: a UNIX-like monolithic operating system">
</p>

<p align="center">
  <a href="https://github.com/kkmonlee/kmOS/actions/workflows/ci.yml"><img src="https://github.com/kkmonlee/kmOS/actions/workflows/ci.yml/badge.svg" alt="Kernel and QEMU tests"></a>
  <a href="https://github.com/kkmonlee/kmOS/actions/workflows/nightly.yml"><img src="https://github.com/kkmonlee/kmOS/actions/workflows/nightly.yml/badge.svg" alt="Nightly validation"></a>
</p>

kmOS is a UNIX-like operating system for 32-bit x86, written in C++ and assembly.
It's a personal project built around a monolithic kernel, with most of the work
focused on memory management and processes.

<p align="center">
  <img src="media/fetch.png" width="720" alt="kmOS running in QEMU">
</p>

## Build and run

You'll need an `i686-elf` GCC/binutils toolchain, NASM, QEMU, make and Python 3.

```sh
make run
```

This builds the kernel and user programs, then boots QEMU with 64 MB of RAM.
User `init` runs `hello` and `cat`, waits for them to finish, and exits to the
kernel shell. No disk image or root access is needed.

| Command | What it does |
| --- | --- |
| `make` | Build the kernel and user programs |
| `make run RAM=128M` | Boot with 128 MB of RAM |
| `make debug` | Boot in the terminal; press Ctrl-a, then x to quit |
| `make test` | Run memory and QEMU integration tests |
| `make iso` | Build a bootable GRUB ISO |
| `make test-iso` | Build and test the GRUB ISO |
| `make version` | Show the version and build identity |

ISO builds need GRUB and xorriso, or a running Docker/OrbStack installation.

In the shell, try `help`, `ls`, `cat /etc/motd`, `mem`, `ps` or `uptime`.
`fetch` and `lsb_release -a` show system information. The version is in [VERSION](VERSION).

## What works

| Area | Current support |
| --- | --- |
| Boot | Multiboot/GRUB, VGA and serial consoles |
| Memory | Paging, physical frame tracking, heap allocators and copy-on-write |
| Processes | Ring-3 ELF programs, timer scheduling, `fork`, `execve` and `waitpid` |
| Files | Paths, working directories, shared descriptors, `dup2` and close-on-exec |
| Userland | `init`, `hello` and `cat`, loaded from boot modules under `/bin` |
| Input | Keyboard and serial input |

User programs have separate address spaces and guarded stacks. Invalid pointers
and user faults terminate the process. The supported syscall wrappers are in
[src/userland/user.h](src/userland/user.h).

## Still to do

- Connect storage drivers to the filesystem and support persistent writes.
- Move the interactive shell into user space and add pipes and signal handling.
- Add SMP support and more hardware drivers.

Only the first 1 GB of RAM is managed. The interactive shell runs in kernel
space, and boot-module files are read-only.

## Screenshots

Memory, processes and uptime:

![kmOS shell running mem, ps and uptime](media/shell.png)

<details>
<summary>Earlier versions</summary>

### [v0.1](https://github.com/kkmonlee/kmOS/tree/v0.1)

![kmOS v0.1 boot screen](media/v0.1-screengrab.png)

### [v0.1.1](https://github.com/kkmonlee/kmOS/tree/v0.1.1)

![kmOS v0.1.1 boot screen](media/v0.1.1-screengrab.png)

</details>

## License

Apache License 2.0.
