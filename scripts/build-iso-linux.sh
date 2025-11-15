#!/bin/bash
# Linux-specific ISO builder with full GRUB support
# This script runs inside Docker or on native Linux

set -e

GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

ISO_DIR="/build/iso"
KERNEL_PATH="/build/src/kernel/kernel.elf"
ISO_OUTPUT="/build/kmos.iso"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  kmOS ISO Builder (Linux/GRUB)${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

echo -e "${BLUE}[1/5]${NC} Checking tools..."

if ! command -v grub-mkrescue &> /dev/null; then
    echo -e "${RED}[ERROR]${NC} grub-mkrescue not found"
    exit 1
fi

echo -e "${GREEN}[OK]${NC} Found grub-mkrescue"

echo ""
echo -e "${BLUE}[2/5]${NC} Preparing ISO structure..."

rm -rf "$ISO_DIR"
mkdir -p "$ISO_DIR/boot/grub"

if [ ! -f "$KERNEL_PATH" ]; then
    echo -e "${RED}[ERROR]${NC} Kernel not found at $KERNEL_PATH"
    echo "Build the kernel first with: make -C src/kernel"
    exit 1
fi

cp "$KERNEL_PATH" "$ISO_DIR/boot/kernel.elf"

# Create grub.cfg
cat > "$ISO_DIR/boot/grub/grub.cfg" << 'EOF'
set timeout=5
set default=0

set color_normal=white/black
set color_highlight=black/light-gray

set menu_color_normal=cyan/blue
set menu_color_highlight=white/blue

menuentry "kmOS - Kernel Mode Operating System" {
    multiboot /boot/kernel.elf
    boot
}

menuentry "kmOS - Safe Mode (No APIC, No ACPI)" {
    multiboot /boot/kernel.elf noapic noacpi
    boot
}

menuentry "kmOS - Debug Mode (Serial Output)" {
    multiboot /boot/kernel.elf debug
    boot
}

menuentry "kmOS - Minimal Mode (Single CPU)" {
    multiboot /boot/kernel.elf maxcpus=1
    boot
}
EOF

echo -e "${GREEN}[OK]${NC} ISO structure prepared"

echo ""
echo -e "${BLUE}[3/5]${NC} Creating bootable ISO with GRUB..."

rm -f "$ISO_OUTPUT"

grub-mkrescue -o "$ISO_OUTPUT" "$ISO_DIR" 2>&1 | grep -v "warning:" || true

if [ ! -f "$ISO_OUTPUT" ]; then
    echo -e "${RED}[ERROR]${NC} ISO creation failed"
    exit 1
fi

echo -e "${GREEN}[OK]${NC} ISO created"

echo ""
echo -e "${BLUE}[4/5]${NC} Verifying ISO..."

ISO_SIZE=$(stat -c%s "$ISO_OUTPUT" 2>/dev/null)
echo -e "${GREEN}[OK]${NC} ISO size: $((ISO_SIZE / 1024 / 1024)) MB"

echo ""
echo -e "${BLUE}[5/5]${NC} Finalizing..."

echo -e "${GREEN}[OK]${NC} Build complete"

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  ISO Build Successful!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "${YELLOW}ISO Location:${NC} /build/kmos.iso"
echo -e "${YELLOW}ISO Size:${NC} $((ISO_SIZE / 1024 / 1024)) MB"
echo ""
