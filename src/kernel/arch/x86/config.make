CC=i686-elf-gcc
SC=i686-elf-g++
ASM=nasm
LD=i686-elf-ld
NM=nm
OBJDUMP=objdump

ASMFLAG=-f elf32
LDFLAG=-m elf_i386 -T arch/x86/linker.ld
FLAG=-m32 -Wall -Wextra -O2 -fno-builtin -nostdinc -fno-stack-protector -nostdlib -fno-exceptions -fno-rtti $(INCDIR)

CFLAGS=$(FLAG)
CXXFLAGS=$(FLAG)
LDFLAGS=$(LDFLAG)