#!/bin/bash

set -e  # Exit on any error

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' 

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

case "$OSTYPE" in
    darwin*)
        echo -e "${YELLOW}[INFO]${NC} Detected macOS - using macOS-compatible builder"
        exec "$SCRIPT_DIR/build-iso-macos.sh"
        ;;
    linux*)

        ;;
    *)
        echo -e "${YELLOW}[WARNING]${NC} Unknown OS: $OSTYPE"
        echo -e "${YELLOW}[INFO]${NC} Attempting Linux method..."
        ;;
esac
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ISO_DIR="$PROJECT_ROOT/iso"
KERNEL_PATH="$PROJECT_ROOT/src/kernel/kernel.elf"
ISO_OUTPUT="$PROJECT_ROOT/kmos.iso"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  kmOS ISO Builder${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

check_tool() {
    if ! command -v "$1" &> /dev/null; then
        echo -e "${RED}[ERROR]${NC} Required tool '$1' not found"
        echo -e "${YELLOW}[INFO]${NC} Install it with:"
        case "$OSTYPE" in
            darwin*)
                echo "  brew install $2"
                ;;
            linux*)
                echo "  sudo apt-get install $2"
                echo "  sudo yum install $2"
                ;;
        esac
        return 1
    fi
    return 0
}

echo -e "${BLUE}[1/6]${NC} Checking for required tools..."

GRUB_MKRESCUE=""
if command -v grub-mkrescue &> /dev/null; then
    GRUB_MKRESCUE="grub-mkrescue"
elif command -v grub2-mkrescue &> /dev/null; then
    GRUB_MKRESCUE="grub2-mkrescue"
else
    echo -e "${RED}[ERROR]${NC} grub-mkrescue not found"
    echo -e "${YELLOW}[INFO]${NC} Install GRUB utilities:"
    case "$OSTYPE" in
        darwin*)
            echo "  brew install grub xorriso"
            echo "  Note: On macOS, you may need to use Docker or a Linux VM"
            ;;
        linux*)
            echo "  sudo apt-get install grub-pc-bin xorriso mtools"
            echo "  sudo yum install grub2-tools xorriso"
            ;;
    esac
    exit 1
fi

echo -e "${GREEN}[OK]${NC} Found $GRUB_MKRESCUE"

if ! check_tool xorriso xorriso; then
    exit 1
fi
echo -e "${GREEN}[OK]${NC} Found xorriso"

echo ""
echo -e "${BLUE}[2/6]${NC} Building kernel..."

cd "$PROJECT_ROOT"
if ! make -C src/kernel > /dev/null 2>&1; then
    echo -e "${RED}[ERROR]${NC} Kernel build failed"
    echo "Run 'make' manually to see errors"
    exit 1
fi

echo -e "${GREEN}[OK]${NC} Kernel built successfully"

if [ ! -f "$KERNEL_PATH" ]; then
    echo -e "${RED}[ERROR]${NC} Kernel not found at $KERNEL_PATH"
    exit 1
fi

KERNEL_SIZE=$(stat -f%z "$KERNEL_PATH" 2>/dev/null || stat -c%s "$KERNEL_PATH" 2>/dev/null)
echo -e "${GREEN}[OK]${NC} Kernel size: $((KERNEL_SIZE / 1024)) KB"

echo ""
echo -e "${BLUE}[3/6]${NC} Preparing ISO directory structure..."

rm -rf "$ISO_DIR"
mkdir -p "$ISO_DIR/boot/grub"

# Copy kernel
cp "$KERNEL_PATH" "$ISO_DIR/boot/kernel.elf"
echo -e "${GREEN}[OK]${NC} Copied kernel to ISO directory"

# Create grub.cfg if it doesn't exist
if [ ! -f "$ISO_DIR/boot/grub/grub.cfg" ]; then
    cat > "$ISO_DIR/boot/grub/grub.cfg" << 'EOF'
# GRUB configuration for kmOS
set timeout=5
set default=0

set color_normal=white/black
set color_highlight=black/light-gray

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
    echo -e "${GREEN}[OK]${NC} Created grub.cfg"
else
    echo -e "${GREEN}[OK]${NC} Using existing grub.cfg"
fi

echo ""
echo -e "${BLUE}[4/6]${NC} Creating ISO image..."

# Remove old ISO if it exists
rm -f "$ISO_OUTPUT"

# Create the ISO
if $GRUB_MKRESCUE -o "$ISO_OUTPUT" "$ISO_DIR" > /dev/null 2>&1; then
    echo -e "${GREEN}[OK]${NC} ISO created successfully"
else
    echo -e "${RED}[ERROR]${NC} Failed to create ISO"
    exit 1
fi

echo ""
echo -e "${BLUE}[5/6]${NC} Verifying ISO..."

if [ ! -f "$ISO_OUTPUT" ]; then
    echo -e "${RED}[ERROR]${NC} ISO file not found after creation"
    exit 1
fi

ISO_SIZE=$(stat -f%z "$ISO_OUTPUT" 2>/dev/null || stat -c%s "$ISO_OUTPUT" 2>/dev/null)
echo -e "${GREEN}[OK]${NC} ISO size: $((ISO_SIZE / 1024 / 1024)) MB"

echo ""
echo -e "${BLUE}[6/6]${NC} Cleaning up temporary files..."

# Keep ISO directory for inspection, but it will be rebuilt on next run
echo -e "${GREEN}[OK]${NC} Build complete"

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  ISO Build Successful!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "${YELLOW}ISO Location:${NC} $ISO_OUTPUT"
echo -e "${YELLOW}ISO Size:${NC} $((ISO_SIZE / 1024 / 1024)) MB"
echo ""
echo -e "${BLUE}Test in QEMU:${NC}"
echo "  qemu-system-i386 -cdrom kmos.iso -m 64M"
echo ""
echo -e "${BLUE}Test in QEMU (with serial output):${NC}"
echo "  qemu-system-i386 -cdrom kmos.iso -m 64M -serial stdio"
echo ""
echo -e "${BLUE}Use in VirtualBox:${NC}"
echo "  1. Create new VM (Type: Other, Version: Other/Unknown)"
echo "  2. Attach kmos.iso as CD/DVD"
echo "  3. Boot from CD"
echo ""
echo -e "${BLUE}Use in VMware:${NC}"
echo "  1. Create new VM (Guest OS: Other, Other)"
echo "  2. Use kmos.iso as CD/DVD image"
echo "  3. Boot from CD"
echo ""
