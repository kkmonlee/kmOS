CROSS ?= i686-elf
CC=$(CROSS)-gcc
SC=$(CROSS)-g++
ASM=nasm
LD=$(CROSS)-ld
NM=$(CROSS)-nm
OBJDUMP=$(CROSS)-objdump

ASMFLAG=-f elf32
LDFLAG=-z noexecstack -m elf_i386 -T arch/x86/linker.ld
FLAG=-MMD -MP -g -m32 -Wall -Wextra -O2 -fno-builtin -nostdinc -fno-stack-protector -nostdlib -fno-exceptions -fno-rtti $(INCDIR)

CFLAGS=$(FLAG)
CXXFLAGS=$(FLAG)
LDFLAGS=$(LDFLAG)