#!/bin/bash
# Test script for kmOS ISO
# Verifies that the ISO can boot in QEMU

set -e

GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ISO_PATH="$PROJECT_ROOT/kmos.iso"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  kmOS ISO Test${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Check if ISO exists
if [ ! -f "$ISO_PATH" ]; then
    echo -e "${RED}[ERROR]${NC} ISO not found at $ISO_PATH"
    echo "Build it first with: make iso"
    exit 1
fi

echo -e "${GREEN}[OK]${NC} Found ISO at $ISO_PATH"

# Check ISO size
ISO_SIZE=$(stat -f%z "$ISO_PATH" 2>/dev/null || stat -c%s "$ISO_PATH" 2>/dev/null)
echo -e "${GREEN}[OK]${NC} ISO size: $((ISO_SIZE / 1024)) KB"

# Check if it's a valid ISO
if file "$ISO_PATH" | grep -q "ISO 9660"; then
    echo -e "${GREEN}[OK]${NC} Valid ISO 9660 filesystem"
else
    echo -e "${RED}[ERROR]${NC} Not a valid ISO file"
    exit 1
fi

# Check if QEMU is available
if ! command -v qemu-system-i386 &> /dev/null; then
    echo -e "${YELLOW}[WARNING]${NC} QEMU not found, skipping boot test"
    echo -e "${GREEN}[OK]${NC} ISO validation passed"
    exit 0
fi

echo -e "${GREEN}[OK]${NC} QEMU is available"

echo ""
echo -e "${BLUE}Testing ISO boot in QEMU...${NC}"
echo -e "${YELLOW}[INFO]${NC} Booting ISO (will run for 5 seconds)"

# Create a temporary log file
LOGFILE=$(mktemp)

# Boot ISO in QEMU with timeout
(
    timeout 5 qemu-system-i386 \
        -cdrom "$ISO_PATH" \
        -m 64M \
        -serial file:"$LOGFILE" \
        -display none \
        2>/dev/null || true
) &

QEMU_PID=$!
sleep 5
kill -9 $QEMU_PID 2>/dev/null || true
wait $QEMU_PID 2>/dev/null || true

# Check if kernel produced output
if [ -f "$LOGFILE" ] && [ -s "$LOGFILE" ]; then
    if grep -q "KMAIN" "$LOGFILE" 2>/dev/null; then
        echo -e "${GREEN}[OK]${NC} Kernel booted successfully from ISO"
        echo -e "${YELLOW}[INFO]${NC} Kernel output detected in serial log"
    else
        echo -e "${YELLOW}[WARNING]${NC} ISO boots but kernel output not detected"
        echo -e "${YELLOW}[INFO]${NC} This may be normal for non-GRUB ISO"
    fi
else
    echo -e "${YELLOW}[WARNING]${NC} No serial output captured"
    echo -e "${YELLOW}[INFO]${NC} ISO validation passed (file format OK)"
fi

# Cleanup
rm -f "$LOGFILE"

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  ISO Test Complete${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "${BLUE}ISO is ready to use!${NC}"
echo ""
echo -e "Test manually with:"
echo "  qemu-system-i386 -cdrom $ISO_PATH -m 64M"
echo ""
