<p align="center">
  <img src="https://raw.githubusercontent.com/kkmonlee/kmOS/master/media/kmos.png">
</p>

# kmOS

kmOS is a general-purpose UNIX-like monolithic operating system written from scratch. Its primary aim is to have interoperability with existing software and architectures; be maintainable, modular and quick.

On the backend, since kmOS is monolithic, it contains a very quick driver model between device drivers and the kernel. This enables the drivers to be written in a forward compatible manner so that the kernel level components can be upgraded without necessarily requiring a recompilation of all device drivers.

kmOS is event-driven, preemptible, SMP-ready, and network ready.

## Current Implementation Status

**Completed Features:**
- **Memory Management**: Full demand paging system with virtual memory
- **Copy-on-Write (COW)**: Efficient fork() implementation with shared pages
- **Memory Allocators**: Buddy, slab, SLOB, SLUB, and unified allocation systems
- **Architecture**: x86-32 support with interrupt handling and successful boot
- **Build System**: Cross-compilation with i686-elf toolchain and dual-mode QEMU support
- **CI/CD Pipeline**: Automated testing with GitHub Actions and QEMU
- **Kernel Core**: Monolithic kernel with modular driver model
- **Process Management**: Basic process structures and scheduling framework
- **Boot System**: Multiboot-compliant kernel with VGA and serial console output

**In Development:**
- Filesystem support (EXT2/3/4 planned)
- Device drivers (IDE, VGA, etc.)
- Userland applications and shell

**Planned:**
- x86-64 architecture support
- SMP and networking capabilities
- Advanced security features

## Technical Specifications

- **Language**: C/C++
- **Architecture**: x86-32 (x86-64 planned)
- **Bootloader**: GRUB-compatible
- **Design**: UNIX-like monolithic kernel
- **Memory**: Demand paging with 4KB pages
- **Build**: Cross-compilation with GCC toolchain

### Detailed Specifications

- Kernel
  - Filesystems **PLANNED**
    - VFS (Virtual File System) architecture
    - EXT2/3/4 support (read-only EXT2 partially implemented)
    - FAT32 and NTFS support
    - XFS support
  - Memory management **IMPLEMENTED**
    - **Demand paging**: Full virtual memory system with page fault handling
    - **Physical frame management**: Bitmap-based allocator supporting up to 4GB RAM
    - **Virtual memory mapping**: Complete page table management
    - **Memory layout**: 
      - Kernel identity map: 0x0 - 0x400000 (4MB)
      - Kernel heap: 0x200000 - 0x800000 (demand-allocated)
      - User space: 0x40000000+ (planned)
    - Shared memory support (planned)
    - Advanced allocators (buddy/slab planned)
  - Process and thread management **IN DEVELOPMENT**
    - **Process structures**: Architecture-specific process management
    - **Scheduling framework**: Basic scheduler infrastructure in place
    - **Context switching**: x86 task switching implemented
    - Multithreading in userland (POSIX threads planned)
    - Locking primitives (mutex, semaphore, condition variable planned)
    - SMP support and kernel preemption (planned)
  - Security **PLANNED**
    - User privilege model with memory protection
    - Mandatory Access Control (MAC) system
    - Kernel-level ASLR and exploit mitigations
  - Networking **PLANNED**
    - TCP/IP stack implementation
    - IPv4 and IPv6 protocol support
    - UDP, TCP, ICMP protocols
    - Network utilities (ARP, DHCP, DNS)
    - POSIX socket API
- Hardware Support **PLANNED**
  - **Device drivers**: Basic VGA, IDE/SATA, USB (HID, mass storage)
  - **Power management**: ACPI/APM support for shutdown and power states
  - **Virtualization**: QEMU/KVM paravirtualization support
  
- Userland **PLANNED**
  - **Shell**: UNIX-like shell with scripting support
  - **Utilities**: Basic UNIX tools (ls, cat, grep, etc.)
  - **Libraries**: LibC implementation with POSIX compatibility
  - **Binary support**: ELF executable format
  - **GUI**: Simple framebuffer-based windowing system
  - **Package management**: Simple package system for applications
- Developer tools **PLANNED**
  - Build tools: GCC/Clang native compilation
  - Development libraries and POSIX APIs
  - Kernel module interface (DKMS-like)
  - System monitoring and debugging tools
  - Kernel tracing capabilities

## Memory Management Architecture

kmOS features a sophisticated demand paging virtual memory system implemented from scratch:

### Virtual Memory Layout
```
0x00000000 - 0x00400000  |  Kernel Identity Map (4MB)
0x00200000 - 0x00800000  |  Kernel Heap (6MB, demand-allocated)
0x40000000+              |  User Space (planned)
```

### Key Features
- **Demand Paging**: Pages allocated automatically on memory access
- **Page Fault Handling**: Integrated with x86 interrupt system (INT 14)
- **Physical Frame Management**: Bitmap-based allocator supporting up to 4GB RAM
- **4KB Pages**: Standard x86 page size with full page table hierarchy
- **Memory Protection**: Foundation for user/kernel space isolation

### Implementation Details
- **Page Directory & Tables**: Complete x86 paging data structures
- **Frame Allocation**: Efficient bitmap tracking of physical memory
- **Virtual Mapping**: Dynamic page mapping/unmapping functions
- **Heap Integration**: Seamless integration with kernel memory allocator

## Build System

### Prerequisites
- i686-elf cross-compiler toolchain (GCC, binutils)
- NASM assembler
- Make build system
- QEMU for testing (optional)

### Build Commands
```bash
make all        # Build kernel, SDK, and userland
make clean      # Clean build artifacts
make run        # Run kernel in QEMU GUI mode
make debug      # Run kernel in terminal debug mode
```

### Development Commands
```bash
make -C src/kernel          # Build kernel only
make -C src/kernel debug    # Generate symbol table
make -C src/kernel dasm     # Disassemble kernel
```

## Testing & CI/CD

kmOS includes comprehensive automated testing with GitHub Actions:

### Continuous Integration
- **Automated Builds**: Cross-compiler toolchain setup and kernel compilation
- **QEMU Testing**: Bootable ISO creation and runtime verification
- **Symbol Analysis**: Verification of essential kernel symbols and memory management
- **Static Analysis**: Code quality checks with cppcheck
- **Security Scanning**: Vulnerability detection and exploit mitigation verification

### Testing Scripts
```bash
# Run comprehensive kernel functionality tests
./tests/test_kernel_functionality.sh src/kernel/kernel.elf

# Run QEMU integration tests  
./tests/qemu_integration_test.sh src/kernel/kernel.elf

# Basic VMM verification
./test_vmm.sh
```

### Nightly Testing
- Extended QEMU testing with multiple configurations
- Memory stress testing (32MB - 256MB)
- COW functionality validation
- Boot performance analysis
- Compatibility testing across QEMU versions

## Screenshots

### Current Development Status
<img src="https://raw.githubusercontent.com/kkmonlee/kmOS/refs/heads/master/media/v0.1-screengrab.png" alt="kmOS v0.1 Boot Screen">

*kmOS v0.1 - Early boot with basic kernel initialization and memory management*

<img src="https://raw.githubusercontent.com/kkmonlee/kmOS/refs/heads/master/media/v0.1.1-screengrab.png" alt="kmOS v0.1.1 Boot Screen">

*kmOS v0.1.1 - Successful kernel boot with architecture initialization and memory management*

### Debug Mode
The kernel now supports dual-mode execution:
- **GUI Mode** (`make run`): QEMU window with VGA display showing boot messages
- **Debug Mode** (`make debug`): Terminal output with detailed serial debugging information

> **Note**: Additional screenshots will be added as new features are implemented, including:
> - Memory management diagnostics
> - Device driver functionality  
> - Filesystem operations
> - User-space applications

---

## Getting Started

### Quick Start
1. **Clone the repository**:
   ```bash
   git clone https://github.com/kkmonlee/kmOS.git
   cd kmOS
   ```

2. **Build the kernel**:
   ```bash
   make all
   ```

3. **Run in QEMU**:
   ```bash
   make run        # GUI mode with QEMU window
   make debug      # Terminal mode with serial output
   ```

## License
kmOS is licensed under the Apache License 2.0. See the project for details.

## Contributing
kmOS is an open-source operating system project. Contributions are welcome! Please feel free to submit issues, feature requests, or pull requests.

**Current priorities:**
- Device driver development (VGA, keyboard, storage)
- Filesystem implementation (EXT2/3)
- User-space application development
- Testing and debugging tools

## Disclaimer
This file (and only this file) has been partly generated using an LLM.