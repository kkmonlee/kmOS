CC=/usr/bin/gcc
SC=/usr/bin/g++
ASM=nasm
LD=ld
NM=nm
OBJDUMP=objdump

ASMFLAG=-f elf32
LDFLAG=-T arch/x86/linker.ld -melf_i386
FLAG=-m32 -Wall -Wextra -O2 -fno-builtin -nostdinc -fno-stack-protector -nostdlib -fno-exceptions -fno-rtti -mno-red-zone $(INCDIR)

CFLAGS=$(FLAG)
CXXFLAGS=$(FLAG)
LDFLAGS=$(LDFLAG)