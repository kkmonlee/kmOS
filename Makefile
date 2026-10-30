CROSS ?= i686-elf
QEMU ?= qemu-system-i386
RAM ?= 64M
KERNEL = src/kernel/kernel.elf
USER_PROGRAM = src/userland/hello.elf
export CROSS

.PHONY: all build run debug test iso test-iso clean check-tools help

all: check-tools
	$(MAKE) -C src/kernel
	$(MAKE) -C src/userland

build: all

check-tools:
	@for tool in $(CROSS)-g++ $(CROSS)-ld nasm; do \
		command -v $$tool >/dev/null || { echo "Missing $$tool. See README.md." >&2; exit 1; }; \
	done

run: all
	$(QEMU) -kernel $(KERNEL) -initrd $(USER_PROGRAM) -append user -m $(RAM) -serial stdio -no-reboot

debug: all
	$(QEMU) -kernel $(KERNEL) -initrd $(USER_PROGRAM) -append user -m $(RAM) -display none -serial mon:stdio -no-reboot

test: all
	@set -e; test_dir=$$(mktemp -d); trap 'rm -rf "$$test_dir"' EXIT; \
	$(CXX) -std=c++17 -fno-builtin -I src/kernel integration_tests/physical_memory_test.cc -o "$$test_dir/memory"; \
	"$$test_dir/memory"
	python3 integration_tests/boot_test.py --qemu $(QEMU) --cross $(CROSS) --kernel $(KERNEL)

iso: all
	./scripts/build-iso.sh

test-iso: iso
	python3 integration_tests/boot_test.py --qemu $(QEMU) --iso kmos.iso

clean:
	$(MAKE) -C src/kernel clean
	$(MAKE) -C src/userland clean
	rm -f src/sdk/bootdisk/kernel.elf iso/boot/kernel.elf kmos.iso

help:
	@echo "make           build kernel"
	@echo "make run       boot in QEMU"
	@echo "make debug     boot in terminal (Ctrl-a x to quit)"
	@echo "make test      run memory and QEMU tests"
	@echo "make test-iso  build and boot a GRUB ISO"
