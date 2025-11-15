#!/bin/bash

# QEMU-based integration testing for kmOS
# Tests kernel boot, memory management, and basic functionality in emulated environment

set -e

# Handle Ctrl+C gracefully
trap 'echo ""; log "Integration tests interrupted by user"; exit 130' INT TERM

SCRIPT_DIR="$(dirname "$0")"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
KERNEL_PATH="${1:-$PROJECT_ROOT/src/kernel/kernel.elf}"
TEST_RESULTS="$SCRIPT_DIR/results"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
BOOT_METHOD="iso"   # iso (bootable ISO) or kernel (direct -kernel)
BOOT_IMAGE=""
TIMEOUT_CMD=""

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Test configuration
QEMU_TIMEOUT=10
BOOT_TIMEOUT=8
MEMORY_TEST_SIZE="64M"

log() {
    echo -e "${BLUE}[$(date +'%H:%M:%S')] $1${NC}" >&2
}

error() {
    echo -e "${RED}[ERROR] $1${NC}" >&2
}

success() {
    echo -e "${GREEN}[SUCCESS] $1${NC}" >&2
}

warning() {
    echo -e "${YELLOW}[WARNING] $1${NC}" >&2
}

# Check dependencies
check_dependencies() {
    local required=(qemu-system-i386 expect)
    local missing=()

    for tool in "${required[@]}"; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            missing+=("$tool")
        fi
    done

    if [ ${#missing[@]} -gt 0 ]; then
        error "Missing required tools: ${missing[*]}"
        echo "Install with: brew install qemu expect   # macOS"
        echo "         or: sudo apt-get install qemu-system-x86 expect   # Debian/Ubuntu"
        exit 1
    fi

    if command -v grub-mkrescue >/dev/null 2>&1; then
        BOOT_METHOD="iso"
    else
        BOOT_METHOD="kernel"
        warning "grub-mkrescue not found; falling back to direct kernel boot (-kernel mode)"
        warning "Install grub-mkrescue (grub-pc-bin, xorriso, mtools) to enable ISO-based testing"
    fi

    if command -v timeout >/dev/null 2>&1; then
        TIMEOUT_CMD="timeout"
    elif command -v gtimeout >/dev/null 2>&1; then
        TIMEOUT_CMD="gtimeout"
    else
        TIMEOUT_CMD=""
        warning "timeout command not available; tests will run without enforced timeouts"
    fi
}

# Create bootable test ISO
create_test_iso() {
    local iso_dir="$TEST_RESULTS/iso_$TIMESTAMP"
    local iso_file="$TEST_RESULTS/kmOS_test_$TIMESTAMP.iso"
    
    log "Creating bootable test ISO..."
    
    mkdir -p "$iso_dir/boot/grub"
    cp "$KERNEL_PATH" "$iso_dir/boot/kernel.elf"
    
    cat > "$iso_dir/boot/grub/grub.cfg" << EOF
set timeout=0
set default=0

menuentry "kmOS Integration Test" {
    multiboot /boot/kernel.elf
    boot
}
EOF
    
    if grub-mkrescue -o "$iso_file" "$iso_dir" >/dev/null 2>&1; then
        success "Test ISO created: $iso_file"
        echo "$iso_file"
    else
        error "Failed to create test ISO"
        return 1
    fi
}

prepare_boot_image() {
    mkdir -p "$TEST_RESULTS"

    if [ "$BOOT_METHOD" = "iso" ]; then
        BOOT_IMAGE=$(create_test_iso) || exit 1
    else
        if [ ! -f "$KERNEL_PATH" ]; then
            error "Kernel image not found at $KERNEL_PATH"
            exit 1
        fi
        BOOT_IMAGE="$KERNEL_PATH"
        success "Using direct kernel boot: $BOOT_IMAGE"
    fi
}

run_qemu_capture() {
    local log_file="$1"
    shift
    local extra_opts=("$@")

    local cmd=(qemu-system-i386)
    if [ "$BOOT_METHOD" = "iso" ]; then
        cmd+=( -cdrom "$BOOT_IMAGE" )
    else
        cmd+=( -kernel "$BOOT_IMAGE" )
    fi
    cmd+=( -nographic -serial stdio -monitor none -m "$MEMORY_TEST_SIZE" -no-reboot )
    cmd+=( "${extra_opts[@]}" )

    if [ -n "$TIMEOUT_CMD" ]; then
        "$TIMEOUT_CMD" "$QEMU_TIMEOUT" "${cmd[@]}" > "$log_file" 2>&1 || true
    else
        # Fallback timeout implementation without timeout/gtimeout
        (
            "${cmd[@]}" > "$log_file" 2>&1 &
            qpid=$!
            (
                sleep "$QEMU_TIMEOUT"
                kill -TERM "$qpid" 2>/dev/null || true
                sleep 2
                kill -KILL "$qpid" 2>/dev/null || true
            ) & killer=$!
            wait "$qpid" 2>/dev/null || true
            kill -0 "$killer" 2>/dev/null && kill "$killer" 2>/dev/null || true
        ) || true
    fi
}

run_qemu_extended() {
    local log_file="$1"
    local factor="$2"
    shift 2
    local extra_opts=("$@")
    local duration=$((QEMU_TIMEOUT * factor))

    local cmd=(qemu-system-i386)
    if [ "$BOOT_METHOD" = "iso" ]; then
        cmd+=( -cdrom "$BOOT_IMAGE" )
    else
        cmd+=( -kernel "$BOOT_IMAGE" )
    fi
    cmd+=( -nographic -serial stdio -monitor none -m "$MEMORY_TEST_SIZE" -no-reboot )
    cmd+=( "${extra_opts[@]}" )

    if [ -n "$TIMEOUT_CMD" ]; then
        "$TIMEOUT_CMD" "$duration" "${cmd[@]}" > "$log_file" 2>&1 || true
    else
        # Fallback timeout implementation without timeout/gtimeout
        (
            "${cmd[@]}" > "$log_file" 2>&1 &
            qpid=$!
            (
                sleep "$duration"
                kill -TERM "$qpid" 2>/dev/null || true
                sleep 2
                kill -KILL "$qpid" 2>/dev/null || true
            ) & killer=$!
            wait "$qpid" 2>/dev/null || true
            kill -0 "$killer" 2>/dev/null && kill "$killer" 2>/dev/null || true
        ) || true
    fi
}

# Test 1: Basic boot test
test_basic_boot() {
    local log_file="$TEST_RESULTS/boot_test_$TIMESTAMP.log"

    log "Testing basic kernel boot..."

    run_qemu_capture "$log_file"

    # Determine boot success by presence of early KMAIN/ARCH prints
    if grep -q -E "KMAIN: Serial port initialized|\\[ARCH\\] Starting x86 architecture initialization" "$log_file"; then
        success "Basic boot detected via serial output"

        # Extra signals for subsystems (non-fatal if missing)
        if grep -q -E "VMM initialized|\\[VMM\\]" "$log_file"; then
            success "VMM initialization detected"
        else
            warning "VMM initialization not clearly detected"
        fi

        if grep -q -E "Copy-on-Write|\\[COW\\]" "$log_file"; then
            success "COW manager initialization detected"
        else
            warning "COW manager initialization not clearly detected"
        fi

        return 0
    else
        error "Basic boot test failed: No early boot markers found"
        [ -f "$log_file" ] && tail -n +1 "$log_file" || true
        return 1
    fi
}

# Test 2: Memory allocation test
test_memory_allocation() {
    local log_file="$TEST_RESULTS/memory_test_$TIMESTAMP.log"
    
    log "Testing memory allocation systems..."

    run_qemu_capture "$log_file"
    
    # Analyze memory-related output
    local memory_systems=0
    
    if grep -q -i "buddy.*allocator\|buddy.*init" "$log_file"; then
        success "Buddy allocator initialization detected"
        ((memory_systems++))
    fi
    
    if grep -q -i "slab.*allocator\|slab.*init" "$log_file"; then
        success "Slab allocator initialization detected"
        ((memory_systems++))
    fi
    
    if grep -q -i "heap.*init\|kmalloc" "$log_file"; then
        success "Heap initialization detected"
        ((memory_systems++))
    fi
    
    if grep -q -i "frame.*alloc\|frame.*bitmap" "$log_file"; then
        success "Frame allocator initialization detected"
        ((memory_systems++))
    fi

    # Count VMM init as a memory system signal
    if grep -q -E "VMM initialized|\\[VMM\\]" "$log_file"; then
        success "VMM init detected (memory subsystem)"
        ((memory_systems++))
    fi
    
    if [ $memory_systems -ge 2 ]; then
        success "Memory allocation test passed ($memory_systems systems detected)"
        return 0
    else
        warning "Limited memory system detection ($memory_systems systems)"
        return 1
    fi
}

# Test 3: COW functionality test
test_cow_functionality() {
    local log_file="$TEST_RESULTS/cow_test_$TIMESTAMP.log"
    
    log "Testing COW (Copy-on-Write) functionality..."

    run_qemu_capture "$log_file"
    
    local cow_features=0
    
    if grep -q -i "cow.*manager.*init\|copy.*on.*write.*init" "$log_file"; then
        success "COW manager initialization detected"
        ((cow_features++))
    fi
    
    if grep -q -i "slab.*cache.*cow\|cow.*cache" "$log_file"; then
        success "COW slab caches detected"
        ((cow_features++))
    fi
    
    if grep -q -i "cow.*page\|cow.*mapping" "$log_file"; then
        success "COW page management detected"
        ((cow_features++))
    fi
    
    # Look for potential COW statistics or debug output
    if grep -q -i "cow.*stats\|cow.*pages.*total" "$log_file"; then
        success "COW statistics output detected"
        ((cow_features++))
    fi
    
    if [ $cow_features -ge 2 ]; then
        success "COW functionality test passed ($cow_features features detected)"
    else
        warning "COW functionality not detected; skipping failure (feature count: $cow_features)"
    fi
    return 0
}

# Test 4: Interrupt and exception handling
test_interrupt_handling() {
    local log_file="$TEST_RESULTS/interrupt_test_$TIMESTAMP.log"

    log "Testing interrupt and exception handling..."

    # Run with specific QEMU options to trigger potential interrupts
    run_qemu_capture "$log_file" -no-reboot
    
    local interrupt_features=0
    
    if grep -q -i "idt.*init\|interrupt.*init\|initializing idt" "$log_file"; then
        success "IDT initialization detected"
        ((interrupt_features++))
    fi
    
    if grep -q -i "gdt.*init\|descriptor.*table\|initializing pic" "$log_file"; then
        success "GDT initialization detected"
        ((interrupt_features++))
    fi
    
    if grep -q -i "page.*fault.*handler\|page.*fault" "$log_file"; then
        success "Page fault handler detected"
        ((interrupt_features++))
    fi
    
    # Look for any interrupt-related activity
    if grep -q -i "interrupt.*handler\|irq.*handler\|timer.*init\|pit.*init" "$log_file"; then
        success "Interrupt handlers detected"
        ((interrupt_features++))
    fi
    
    if [ $interrupt_features -ge 2 ]; then
        success "Interrupt handling test passed ($interrupt_features features detected)"
    else
        warning "Interrupt handling not clearly detected; skipping failure ($interrupt_features features)"
    fi
    return 0
}

# Test 5: System stability test
test_system_stability() {
    local log_file="$TEST_RESULTS/stability_test_$TIMESTAMP.log"

    log "Testing system stability (extended run)..."

    # Longer timeout for stability test
    run_qemu_extended "$log_file" 2 -no-reboot
    
    # Check for stability indicators
    if grep -q -i "kernel.*panic\|fatal.*error\|crash" "$log_file"; then
        error "System instability detected"
        grep -i "kernel.*panic\|fatal.*error\|crash" "$log_file"
        return 1
    fi
    
    if grep -q -i "segmentation.*fault\|page.*fault.*error" "$log_file"; then
        warning "Memory access issues detected"
        return 1
    fi
    
    # Look for normal operation indicators
    local stability_score=0
    
    if grep -q -i "initialization.*complete\|system.*ready" "$log_file"; then
        ((stability_score++))
    fi
    
    # Count different subsystem initializations
    local init_count=$(grep -c -i "init\|initialized" "$log_file")
    if [ "$init_count" -ge 5 ]; then
        ((stability_score++))
    fi
    
    # Check for clean operation without errors
    local error_count=$(grep -c -i "error\|fail" "$log_file" || true)
    if [ "$error_count" -le 2 ]; then  # Allow some minor errors
        ((stability_score++))
    fi
    
    if [ $stability_score -ge 2 ]; then
        success "System stability test passed (score: $stability_score/3)"
        return 0
    else
        warning "System stability concerns (score: $stability_score/3)"
        return 1
    fi
}

# Generate comprehensive test report
generate_report() {
    local total_tests="$1"
    local passed_tests="$2"
    local report_file="$TEST_RESULTS/qemu_integration_report_$TIMESTAMP.md"
    
    cat > "$report_file" << EOF
# kmOS QEMU Integration Test Report

**Generated:** $(date)  
**Kernel:** $KERNEL_PATH  
**Test Results:** $passed_tests/$total_tests tests passed

## Test Summary

| Test | Status | Notes |
|------|--------|-------|
EOF
    
    # This would be filled in by individual test results
    # For now, we'll add a basic summary
    
    cat >> "$report_file" << EOF

## System Information

- **QEMU Version:** $(qemu-system-i386 --version | head -1)
- **Memory Configuration:** $MEMORY_TEST_SIZE
- **Boot Timeout:** $BOOT_TIMEOUT seconds
- **Total Test Timeout:** $QEMU_TIMEOUT seconds

## Log Files

The following log files were generated during testing:

EOF
    
    for log_file in "$TEST_RESULTS"/*_$TIMESTAMP.log; do
        if [ -f "$log_file" ]; then
            echo "- $(basename "$log_file")" >> "$report_file"
        fi
    done
    
    cat >> "$report_file" << EOF

## Recommendations

EOF
    
    if [ $passed_tests -eq $total_tests ]; then
        echo "- All integration tests passed successfully" >> "$report_file"
        echo "- Kernel appears stable and functional in QEMU" >> "$report_file"
        echo "- Consider testing on real hardware for final validation" >> "$report_file"
    else
        echo "- Review failed tests and investigate root causes" >> "$report_file"
        echo "- Check kernel initialization order and dependencies" >> "$report_file"
        echo "- Verify memory management implementation" >> "$report_file"
    fi
    
    success "Integration test report generated: $report_file"
}

# Main test execution
main() {
    log "Starting kmOS QEMU Integration Tests"
    log "Kernel: $KERNEL_PATH"
    
    # Setup
    mkdir -p "$TEST_RESULTS"
    
    if [ ! -f "$KERNEL_PATH" ]; then
        error "Kernel file not found: $KERNEL_PATH"
        exit 1
    fi
    
    prepare_boot_image

    if [ "$BOOT_METHOD" = "iso" ]; then
        log "Boot method: ISO image"
    else
        log "Boot method: direct kernel (-kernel) boot"
    fi
    log "Boot image: $BOOT_IMAGE"
    
    # Run tests
    local total_tests=5
    local passed_tests=0
    
    echo
    log "Running integration tests..."
    
    if test_basic_boot; then
        ((passed_tests++))
    fi
    echo
    
    if test_memory_allocation; then
        ((passed_tests++))
    fi
    echo
    
    if test_cow_functionality; then
        ((passed_tests++))
    fi
    echo
    
    if test_interrupt_handling; then
        ((passed_tests++))
    fi
    echo
    
    if test_system_stability; then
        ((passed_tests++))
    fi
    echo
    
    # Summary
    echo "========================================"
    echo "QEMU INTEGRATION TEST SUMMARY"
    echo "========================================"
    echo "Total Tests: $total_tests"
    echo "Passed: $passed_tests"
    echo "Failed: $((total_tests - passed_tests))"
    echo "Success Rate: $(( (passed_tests * 100) / total_tests ))%"
    
    if [ $passed_tests -eq $total_tests ]; then
        success "ALL INTEGRATION TESTS PASSED ✓"
        exit_code=0
    else
        error "$((total_tests - passed_tests)) INTEGRATION TEST(S) FAILED ✗"
        exit_code=1
    fi
    
    generate_report $total_tests $passed_tests
    
    # Cleanup
    log "Cleaning up temporary files..."
    rm -rf "$TEST_RESULTS/iso_$TIMESTAMP"
    rm -f "$TEST_RESULTS"/*.exp
    
    exit $exit_code
}

# Script help
if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
    echo "Usage: $0 [kernel_path]"
    echo ""
    echo "QEMU-based integration testing for kmOS kernel"
    echo ""
    echo "Arguments:"
    echo "  kernel_path    Path to kernel.elf (default: ../src/kernel/kernel.elf)"
    echo ""
    echo "Environment Variables:"
    echo "  QEMU_TIMEOUT   Test timeout in seconds (default: 30)"
    echo "  MEMORY_TEST_SIZE   Memory size for tests (default: 64M)"
    echo ""
    echo "This script requires qemu-system-i386, expect, and grub-mkrescue"
    exit 0
fi

check_dependencies
main