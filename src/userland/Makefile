CROSS ?= i686-elf

.PHONY: all clean

all: hello.elf

hello.o: hello.asm
	nasm -f elf32 $< -o $@

hello.elf: hello.o linker.ld
	$(CROSS)-ld -m elf_i386 -z noexecstack -T linker.ld -o $@ hello.o

clean:
	rm -f hello.o hello.elf
