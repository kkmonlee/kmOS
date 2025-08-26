SDKDIR=./sdk

help:
	@echo "Makefile for kmOS"
	@echo "Usage: make [ all | clean | help | build | run] " 
	@echo ""
	@echo

all:
	@echo "Building kernel..."
	make -C ./kernel
	@echo "Building SDK..."
	make -C ./sdk
	@echo "Building userland..."
	make -C ./userland

build:
	zip -r kmos-$(VERSION).zip ./

run:
	@echo "Running kmOS in GUI mode"
	@if [[ "$$OSTYPE" != "darwin"* ]]; then \
		cd ./sdk && sudo bash ./diskimage.sh; \
	else \
		echo "Skipping diskimage.sh on macOS (requires Linux tools)"; \
	fi
	cd ./sdk && ./qemu.sh

debug:
	@echo "Running kmOS in debug mode (terminal)"
	@if [[ "$$OSTYPE" != "darwin"* ]]; then \
		cd ./sdk && sudo bash ./diskimage.sh; \
	else \
		echo "Skipping diskimage.sh on macOS (requires Linux tools)"; \
	fi
	cd ./sdk && ./qemu.sh debug

clean:
	make -C ./kernel clean
	make -C ./userland clean
