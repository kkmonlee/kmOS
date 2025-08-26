#!/bin/bash

# Check for debug mode
if [[ "$1" == "debug" ]]; then
    DEBUG_MODE=1
    echo "Running in debug mode (terminal output)..."
else
    DEBUG_MODE=0
    echo "Running in GUI mode (QEMU window)..."
fi

# Check if running on macOS and adjust accordingly
if [[ "$OSTYPE" == "darwin"* ]]; then
    # On macOS, run QEMU with kernel directly
    if [[ -f "./bootdisk/kernel.elf" ]]; then
        if [[ $DEBUG_MODE -eq 1 ]]; then
            # Debug mode: terminal output with serial
            echo "Debug mode: Serial output in terminal"
            qemu-system-i386 -kernel ./bootdisk/kernel.elf \
                -m 64M \
                -nographic \
                -no-reboot
        else
            # GUI mode: QEMU window with VGA display
            echo "GUI mode: QEMU window with VGA display"
            qemu-system-i386 -kernel ./bootdisk/kernel.elf \
                -m 64M \
                -no-reboot
        fi
    else
        echo "Error: kernel.elf not found in bootdisk/"
        exit 1
    fi
else
    # On Linux, use disk image
    if [[ -f "c.img" ]]; then
        if [[ $DEBUG_MODE -eq 1 ]]; then
            qemu-system-i386 -hda c.img -boot c -serial stdio -nographic
        else
            qemu-system-i386 -hda c.img -boot c
        fi
    else
        echo "Error: c.img not found. Run diskimage.sh first."
        exit 1
    fi
fi