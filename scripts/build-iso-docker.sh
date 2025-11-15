#!/bin/bash
# Build kmOS ISO using Docker (works on any platform)
# This provides a consistent Linux build environment with GRUB support

set -e

GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  kmOS Docker ISO Builder${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Check if Docker is available
if ! command -v docker &> /dev/null; then
    echo -e "${RED}[ERROR]${NC} Docker not found"
    echo -e "${YELLOW}[INFO]${NC} Install Docker from: https://www.docker.com/"
    echo -e "${YELLOW}[INFO]${NC} Or use native build: ./scripts/build-iso.sh"
    exit 1
fi

echo -e "${GREEN}[OK]${NC} Docker is available"

echo ""
echo -e "${BLUE}[1/4]${NC} Building kernel..."

# Build kernel on host (faster than in Docker)
cd "$PROJECT_ROOT"
if ! make -C src/kernel > /dev/null 2>&1; then
    echo -e "${RED}[ERROR]${NC} Kernel build failed"
    exit 1
fi

echo -e "${GREEN}[OK]${NC} Kernel built"

echo ""
echo -e "${BLUE}[2/4]${NC} Building Docker image..."

if ! docker build -t kmos-iso-builder -f Dockerfile.iso . > /dev/null 2>&1; then
    echo -e "${RED}[ERROR]${NC} Docker image build failed"
    exit 1
fi

echo -e "${GREEN}[OK]${NC} Docker image ready"

echo ""
echo -e "${BLUE}[3/4]${NC} Creating ISO in Docker container..."

# Run ISO builder in container
docker run --rm \
    -v "$PROJECT_ROOT:/build" \
    kmos-iso-builder

if [ ! -f "$PROJECT_ROOT/kmos.iso" ]; then
    echo -e "${RED}[ERROR]${NC} ISO not found after Docker build"
    exit 1
fi

echo -e "${GREEN}[OK]${NC} ISO created"

echo ""
echo -e "${BLUE}[4/4]${NC} Finalizing..."

ISO_SIZE=$(stat -f%z "$PROJECT_ROOT/kmos.iso" 2>/dev/null || stat -c%s "$PROJECT_ROOT/kmos.iso" 2>/dev/null)

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  Docker ISO Build Complete!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "${YELLOW}ISO Location:${NC} $PROJECT_ROOT/kmos.iso"
echo -e "${YELLOW}ISO Size:${NC} $((ISO_SIZE / 1024 / 1024)) MB"
echo ""
echo -e "${BLUE}Test with:${NC}"
echo "  qemu-system-i386 -cdrom kmos.iso -m 64M"
echo ""
echo -e "${BLUE}Or use in VirtualBox/VMware:${NC}"
echo "  1. Create new VM (Other/Unknown OS)"
echo "  2. Attach kmos.iso as CD/DVD"
echo "  3. Boot from CD"
echo ""
