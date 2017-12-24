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
	echo "Building userland..."
	make -C ./userland

build:
	zip -r kmos-$(VERSION).zip ./

run:
	@echo "Running kmOS"
	cd ./sdk && sudo bask ./diskimage.sh
	cd ./sdk && ./qemu.sh

clean:
	make -C ./kernel clean
	make -C ./userland clean
