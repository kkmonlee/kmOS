CROSS ?= i686-elf
PROGRAMS = hello.elf init.elf cat.elf
CFLAGS = -std=c11 -m32 -Os -g -Wall -Wextra -ffreestanding -fno-builtin -fno-pic -fno-pie -fno-stack-protector

.PHONY: all clean
all: $(PROGRAMS)

%.o: %.asm
	nasm -f elf32 $< -o $@

%.o: %.c user.h
	$(CROSS)-gcc $(CFLAGS) -c $< -o $@

hello.elf: hello.o linker.ld
	$(CROSS)-ld -m elf_i386 -z noexecstack -T linker.ld -o $@ hello.o

init.elf cat.elf: %.elf: %.o start.o linker.ld
	$(CROSS)-ld -m elf_i386 -z noexecstack -T linker.ld -o $@ start.o $*.o

clean:
	rm -f *.o $(PROGRAMS)
