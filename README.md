# kmOS

My x86 kernel in C++. Mostly working on memory management.

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
