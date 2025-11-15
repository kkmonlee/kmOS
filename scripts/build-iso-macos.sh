#!/bin/bash
# Alternative ISO builder for macOS using xorriso/mkisofs
# This script creates a bootable ISO without requiring grub-mkrescue

set -e

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ISO_DIR="$PROJECT_ROOT/iso"
KERNEL_PATH="$PROJECT_ROOT/src/kernel/kernel.elf"
ISO_OUTPUT="$PROJECT_ROOT/kmos.iso"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  kmOS ISO Builder (macOS Compatible)${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

if command -v xorriso &> /dev/null; then
    ISO_TOOL="xorriso"
elif command -v mkisofs &> /dev/null; then
    ISO_TOOL="mkisofs"
elif command -v genisoimage &> /dev/null; then
    ISO_TOOL="genisoimage"
else
    echo -e "${RED}[ERROR]${NC} No ISO creation tool found"
    echo -e "${YELLOW}[INFO]${NC} Install xorriso via Homebrew:"
    echo "  brew install xorriso"
    exit 1
fi

echo -e "${GREEN}[OK]${NC} Using $ISO_TOOL for ISO creation"

echo ""
echo -e "${BLUE}[1/5]${NC} Building kernel..."

cd "$PROJECT_ROOT"
if ! make -C src/kernel > /dev/null 2>&1; then
    echo -e "${RED}[ERROR]${NC} Kernel build failed"
    exit 1
fi

if [ ! -f "$KERNEL_PATH" ]; then
    echo -e "${RED}[ERROR]${NC} Kernel not found"
    exit 1
fi

KERNEL_SIZE=$(stat -f%z "$KERNEL_PATH")
echo -e "${GREEN}[OK]${NC} Kernel size: $((KERNEL_SIZE / 1024)) KB"

echo ""
echo -e "${BLUE}[2/5]${NC} Preparing ISO directory..."

rm -rf "$ISO_DIR"
mkdir -p "$ISO_DIR/boot/grub"

# Copy kernel
cp "$KERNEL_PATH" "$ISO_DIR/boot/kernel.elf"

# Create grub.cfg
cat > "$ISO_DIR/boot/grub/grub.cfg" << 'EOF'
set timeout=5
set default=0

set color_normal=white/black
set color_highlight=black/light-gray

menuentry "kmOS - Kernel Mode Operating System" {
    multiboot /boot/kernel.elf
    boot
}

menuentry "kmOS - Safe Mode" {
    multiboot /boot/kernel.elf noapic
    boot
}

menuentry "kmOS - Debug Mode" {
    multiboot /boot/kernel.elf debug
    boot
}
EOF

echo -e "${GREEN}[OK]${NC} Created ISO structure"

echo ""
echo -e "${BLUE}[3/5]${NC} Checking for GRUB files..."

# Try to find GRUB files
GRUB_DIR=""
for dir in /usr/lib/grub/i386-pc /usr/share/grub/i386-pc /opt/homebrew/lib/grub/i386-pc; do
    if [ -d "$dir" ]; then
        GRUB_DIR="$dir"
        break
    fi
done

if [ -z "$GRUB_DIR" ]; then
    echo -e "${YELLOW}[WARNING]${NC} GRUB files not found"
    echo -e "${YELLOW}[INFO]${NC} Creating minimal bootable ISO without GRUB..."
    echo -e "${YELLOW}[INFO]${NC} This ISO will work with 'qemu-system-i386 -kernel' mode"
    
    # Create a simple El Torito bootable ISO
    rm -f "$ISO_OUTPUT"
    
    if [ "$ISO_TOOL" = "xorriso" ]; then
        xorriso -as mkisofs \
            -o "$ISO_OUTPUT" \
            -b boot/kernel.elf \
            -no-emul-boot \
            -boot-load-size 4 \
            -boot-info-table \
            "$ISO_DIR" > /dev/null 2>&1
    else
        $ISO_TOOL -o "$ISO_OUTPUT" \
            -b boot/kernel.elf \
            -no-emul-boot \
            -boot-load-size 4 \
            -boot-info-table \
            "$ISO_DIR" > /dev/null 2>&1
    fi
else
    echo -e "${GREEN}[OK]${NC} Found GRUB at $GRUB_DIR"
    
    # Copy GRUB files
    mkdir -p "$ISO_DIR/boot/grub/i386-pc"
    cp "$GRUB_DIR"/*.mod "$ISO_DIR/boot/grub/i386-pc/" 2>/dev/null || true
    cp "$GRUB_DIR"/*.img "$ISO_DIR/boot/grub/i386-pc/" 2>/dev/null || true
    
    echo ""
    echo -e "${BLUE}[4/5]${NC} Creating bootable ISO with GRUB..."
    
    rm -f "$ISO_OUTPUT"
    
    if [ "$ISO_TOOL" = "xorriso" ]; then
        xorriso -as mkisofs \
            -o "$ISO_OUTPUT" \
            -b boot/grub/i386-pc/eltorito.img \
            -no-emul-boot \
            -boot-load-size 4 \
            -boot-info-table \
            "$ISO_DIR" > /dev/null 2>&1 || {
            echo -e "${YELLOW}[WARNING]${NC} GRUB boot failed, creating simple ISO..."
            xorriso -as mkisofs -o "$ISO_OUTPUT" "$ISO_DIR" > /dev/null 2>&1
        }
    else
        $ISO_TOOL -o "$ISO_OUTPUT" -R -J "$ISO_DIR" > /dev/null 2>&1
    fi
fi

echo ""
echo -e "${BLUE}[5/5]${NC} Verifying ISO..."

if [ ! -f "$ISO_OUTPUT" ]; then
    echo -e "${RED}[ERROR]${NC} ISO creation failed"
    exit 1
fi

ISO_SIZE=$(stat -f%z "$ISO_OUTPUT")
echo -e "${GREEN}[OK]${NC} ISO size: $((ISO_SIZE / 1024 / 1024)) MB"

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  ISO Build Successful!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "${YELLOW}ISO Location:${NC} $ISO_OUTPUT"
echo -e "${YELLOW}ISO Size:${NC} $((ISO_SIZE / 1024 / 1024)) MB"
echo ""
echo -e "${RED}⚠️  IMPORTANT:${NC} This ISO does not have GRUB bootloader!"
echo -e "${YELLOW}[INFO]${NC} macOS xorriso cannot create bootable multiboot ISOs"
echo ""
echo -e "${BLUE}To boot kmOS, use QEMU's direct kernel mode:${NC}"
echo "  qemu-system-i386 -kernel src/kernel/kernel.elf -m 64M"
echo ""
echo -e "${BLUE}Or use the Docker build for a proper bootable ISO:${NC}"
echo "  ./scripts/build-iso-docker.sh"
echo ""
echo -e "${YELLOW}Note:${NC} The ISO created here is for file distribution only."
echo "It contains the kernel but cannot boot directly from CD/DVD."
echo ""
echo -e "${BLUE}For VM testing:${NC}"
echo "  • VirtualBox/VMware: Use Docker-built ISO"
echo "  • QEMU: Use -kernel flag (fastest method)"
echo ""
