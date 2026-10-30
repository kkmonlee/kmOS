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

`make run` and the GRUB ISO load `src/userland/hello.elf` as a boot module.
It runs in ring 3, prints a message, exits, and returns to the kernel shell.
The loader accepts static i386 ELF executables with separate loadable pages.
User programs currently have `write` (syscall 4, console descriptors 1 and 2)
and `exit` (syscall 1) through `int 0x80`. EAX selects the syscall,
EBX/ECX/EDX carry arguments, and EAX holds the result.
Invalid pointers and user faults terminate the process.

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
