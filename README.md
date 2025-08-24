<p align="center">
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
  - POSIX / ANSI (hopefully)
  - LibC
  - Can run Shell and Lua

### Detailed Specifications

- Kernel
  - Filesystems
    - EXT3, EXT4, XFS
    - VFS
    - FAT32 and NTFS
  - Memory management
    - Demand paging
    - Shared memory support using POSIX API `shm` or System V IPC
    - Non-contiguous memory support using buddy or slab allocator
  - Process and thread management
    - Multithreading in userland (POSIX threads)
    - Locking primitives (mutex, semaphore, condition variable)
    - Lock-free algorithms for SMP scalability (atomic operations)
    - Kernel preemption (SMP)
  - Security
    - User priviledge model
    - SELinux-like MAC
    - Kernel-level ASLR
  - Networking
    - TCP/IP stack
    - IPv4 and IPv6
    - UDP, TCP, ICMP
    - ARP, DHCP, DNS
    - Socket API
- Device/hardware features
  - Device drivers
    - USB support (HID, mass storage)
    - GPU hardware acceleration or a basic framebuffer console
    - Sound card support (ALSA-like API)
  - Power management
    - ACPI
    - APM
    - CPU frequency scaling
  - Virtualization
    - KVM or QEMU paravirtualization
    - Virtual device drivers
- Userland
  - Shell
    - Supports scripting - Lua, maybe Python or Perl later on
    - Basic UNIX utilities (ls, cat, grep, etc.)
  - Libraries
    - Dynamic linking (ELF)
    - Common binary format compatibility (Wine-like layer for Windows binaries)
  - GUI
    - Basic windowing system (OpenGL or framebuffer-based)
    - GUI toolkit (idk, maybe Qt or GTK)
  - Package management
    - Package manager (like pacman)
    - Package format (like .rpm)
- Developer tools
  - Build tools
    - GCC, Clang
    - GDB server
  - Dev libraries
    - System APIs (POSIX)
    - Interface for kernel modules (like DKMS, `module_init` and `module_exit`)
    - Kernel log viewer (like `dmesg`)
    - Maybe system monitoring (like `top`, `iotop`, `htop`)
    - Kernel tracing (like `strace`, `ltrace`)

## Screenshots
<img src="https://raw.githubusercontent.com/kkmonlee/kmOS/refs/heads/master/media/v0.1-screengrab.png">