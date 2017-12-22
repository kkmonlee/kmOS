<p align="center">
  <!--<b>Some Links:</b><br>
  <a href="#">Link 1</a> |
  <a href="#">Link 2</a> |
  <a href="#">Link 3</a>
  <br><br>-->
  <img src="https://raw.githubusercontent.com/kkmonlee/kmOS/master/media/kmos.png">
</p>

# kmOS
kmOS is a general-purpose UNIX-like monolithic operating system written from scratch. Its primary aim is to have interoperability with existing software and architectures; be maintainable, modular and quick.

On the backend, since kmOS is monolithic, it contains a very quick driver model between device drivers and the kernel. This enables the drivers to be written in a forward compatible manner so that the kernel level components can be upgraded without necessarily requiring a recompilation of all device drivers.

kmOS is event-driven, preemptible, SMP-ready, and network ready.

## Specifications
- C/C++
- x86-32- and x86-64-bit architecture
- GRUB bootloader
- UNIX-like
- Event-driven
- Preemptible
- SMP-ready
- Network ready
- ELF executable in userland
- Modules accessible in userland
  - IDE disks
  - DOS partitions
  - Clock
  - EXT2 (read only)
  - Boch VBE
- Userland
  - POSIX
  - LibC
  - Can run Shell and Lua