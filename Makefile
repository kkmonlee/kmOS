SRCDIR = ./src

.PHONY: all build run debug clean help iso

all:
	$(MAKE) -C $(SRCDIR) all

build:
	$(MAKE) -C $(SRCDIR) build

run:
	$(MAKE) -C $(SRCDIR) run

debug:
	$(MAKE) -C $(SRCDIR) debug

iso:
	@echo "Building bootable ISO image..."
	@./scripts/build-iso.sh

clean:
	$(MAKE) -C $(SRCDIR) clean
	@rm -f kmos.iso
	@rm -rf iso
	@echo "Cleaned ISO artifacts"

help:
	$(MAKE) -C $(SRCDIR) help
	@echo ""
	@echo "ISO Targets:"
	@echo "  make iso       Build bootable ISO image (kmos.iso)"
	@echo ""
	@echo "ISO can be tested with:"
	@echo "  qemu-system-i386 -cdrom kmos.iso -m 64M"