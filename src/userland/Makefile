CROSS ?= i686-elf
PROGRAMS = hello.elf init.elf cat.elf sh.elf ls.elf
CFLAGS = -std=c11 -m32 -Os -g -Wall -Wextra -ffreestanding -fno-builtin -fno-pic -fno-pie -fno-stack-protector
ASMFLAGS = -f elf32
LDFLAGS = -m elf_i386 -z noexecstack -T linker.ld

ifeq ($(WERROR),1)
CFLAGS += -Werror
ASMFLAGS += -Werror
LDFLAGS += --fatal-warnings
endif

.PHONY: all clean
all: $(PROGRAMS)

%.o: %.asm
	nasm $(ASMFLAGS) $< -o $@

%.o: %.c user.h ../sdk/include/fcntl.h ../sdk/include/dirent.h ../sdk/include/signal.h ../sdk/include/sys/stat.h ../sdk/include/sys/wait.h
	$(CROSS)-gcc $(CFLAGS) -c $< -o $@

hello.elf: hello.o linker.ld
	$(CROSS)-ld $(LDFLAGS) -o $@ hello.o

init.elf cat.elf sh.elf ls.elf: %.elf: %.o start.o alloc.o linker.ld
	$(CROSS)-ld $(LDFLAGS) -o $@ start.o alloc.o $*.o

clean:
	rm -f *.o $(PROGRAMS)
