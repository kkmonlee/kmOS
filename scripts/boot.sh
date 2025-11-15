#!/bin/bash
# Quick boot script for kmOS
# Uses the most reliable boot method for the current platform

set -e

GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
KERNEL="$PROJECT_ROOT/src/kernel/kernel.elf"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  kmOS Quick Boot${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

if [ ! -f "$KERNEL" ]; then
    echo -e "${YELLOW}[INFO]${NC} Kernel not found, building..."
    cd "$PROJECT_ROOT"
    make -C src/kernel
fi

if [ ! -f "$KERNEL" ]; then
    echo -e "${RED}[ERROR]${NC} Failed to build kernel"
    exit 1
fi

echo -e "${GREEN}[OK]${NC} Kernel ready at $KERNEL"
echo ""

# Check for QEMU
if ! command -v qemu-system-i386 &> /dev/null; then
    echo -e "${RED}[ERROR]${NC} QEMU not found"
    echo "Install with: brew install qemu"
    exit 1
fi

echo -e "${GREEN}[OK]${NC} QEMU found"
echo ""

# Determine boot mode
MODE="${1:-gui}"

case "$MODE" in
    gui)
        echo -e "${BLUE}Booting kmOS in GUI mode...${NC}"
        echo -e "${YELLOW}[INFO]${NC} Press Ctrl+Alt+Q or close window to quit"
        echo ""
        qemu-system-i386 \
            -kernel "$KERNEL" \
            -m 64M
        ;;
    
    debug)
        echo -e "${BLUE}Booting kmOS in debug mode (serial only)...${NC}"
        echo -e "${YELLOW}[INFO]${NC} Press Ctrl+C to quit"
        echo ""
        qemu-system-i386 \
            -kernel "$KERNEL" \
            -m 64M \
            -serial stdio \
            -nographic
        ;;
    
    test)
        echo -e "${BLUE}Booting kmOS in test mode (5 seconds)...${NC}"
        echo ""
        
        # Start QEMU in background
        qemu-system-i386 \
            -kernel "$KERNEL" \
            -m 64M \
            -serial stdio \
            -nographic &
        
        QEMU_PID=$!
        sleep 5
        kill $QEMU_PID 2>/dev/null || true
        wait $QEMU_PID 2>/dev/null || true
        
        echo ""
        echo -e "${GREEN}[OK]${NC} Test boot complete"
        ;;
    
    *)
        echo -e "${RED}[ERROR]${NC} Unknown mode: $MODE"
        echo ""
        echo "Usage: $0 [mode]"
        echo ""
        echo "Modes:"
        echo "  gui     Boot with VGA display (default)"
        echo "  debug   Boot with serial console only"
        echo "  test    Boot for 5 seconds and exit"
        exit 1
        ;;
esac
