<p align="center">
  <img src="media/kmos.png" width="520" alt="kmOS: a UNIX-like monolithic operating system">
</p>

<p align="center">
  <a href="https://github.com/kkmonlee/kmOS/actions/workflows/ci.yml"><img src="https://github.com/kkmonlee/kmOS/actions/workflows/ci.yml/badge.svg" alt="Kernel and QEMU tests"></a>
  <a href="https://github.com/kkmonlee/kmOS/actions/workflows/nightly.yml"><img src="https://github.com/kkmonlee/kmOS/actions/workflows/nightly.yml/badge.svg" alt="Nightly validation"></a>
</p>

My x86 kernel in C++. Mostly working on memory management.

Boots through Multiboot/GRUB with VGA and serial consoles.

## Running it

Needs i686-elf GCC/binutils, NASM, QEMU, make and Python 3.

```
make
make run
make debug     # terminal, Ctrl-a x to quit
make test
make test-iso  # needs GRUB + xorriso, or Docker/OrbStack
```

No disk image or root access needed. `RAM=128M make run` to change RAM.
Run `fetch` or `lsb_release -a` in the shell for system info.

Paging is on. Only the first 1 GB of RAM is managed for now.
The shell runs in kernel space. Userland and filesystems still need work.

`make run` and the GRUB ISO load `init`, `hello` and `cat` as boot modules
under `/bin`. User `init` forks, execs the other programs, waits for them,
and exits before the kernel shell starts. `cat` reads `/etc/motd`.
The loader accepts static i386 ELF executables with separate loadable pages.
Syscalls use `int 0x80`: EAX selects the call, EBX/ECX/EDX carry arguments,
and EAX holds the result or a negative error. Available calls are `exit` (1),
`fork` (2), `read` (3), `write` (4), `open` (5), `close` (6), `waitpid` (7),
`execve` (11), `chdir` (12), `getpid` (20), `dup2` (63) and `getppid` (64).
`waitpid` accepts a child PID or -1, with optional `WNOHANG` (1).
Normal exit status uses the high byte; a fault vector uses the low bits.
Open supports access modes 0, 1 and 2, plus `O_CLOEXEC` (0x80000).
Console input returns -11 when no data is ready. Boot modules are read-only.
Invalid pointers and user faults terminate the process. Storage writes,
process groups, pipes and signal handlers still need work.

## Screenshots

![kmOS logo and system info in QEMU](media/fetch.png)

Memory, processes and uptime:

![kmOS shell running mem, ps and uptime](media/shell.png)

<details>
<summary>Earlier boots</summary>

v0.1:

![kmOS v0.1 boot screen](media/v0.1-screengrab.png)

v0.1.1:

![kmOS v0.1.1 boot screen](media/v0.1.1-screengrab.png)

</details>

Apache License 2.0.
