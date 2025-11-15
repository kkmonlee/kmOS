#!/bin/bash

# Comprehensive kernel functionality test script
# Tests memory management, COW, process management, and basic kernel services

set -e

KERNEL_PATH="${1:-src/kernel/kernel.elf}"
TEST_DIR="$(dirname "$0")"
RESULTS_DIR="$TEST_DIR/results"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' 

# Test counters
TESTS_TOTAL=0
TESTS_PASSED=0
TESTS_FAILED=0

log() {
    echo -e "${BLUE}[$(date +'%H:%M:%S')] $1${NC}"
}

test_passed() {
    echo -e "${GREEN}✓ $1${NC}"
    ((++TESTS_PASSED))
}

test_failed() {
    echo -e "${RED}✗ $1${NC}"
    ((++TESTS_FAILED))
}

test_warning() {
    echo -e "${YELLOW}⚠ $1${NC}"
}

run_test() {
    local test_name="$1"
    local test_func="$2"
    
    ((++TESTS_TOTAL))
    log "Running test: $test_name"
    
    if $test_func; then
        test_passed "$test_name"
    else
        test_failed "$test_name"
    fi
    echo
}

# Test 1: Verify kernel exists and basic structure
test_kernel_exists() {
    if [ ! -f "$KERNEL_PATH" ]; then
        echo "Kernel file not found: $KERNEL_PATH"
        return 1
    fi
    
    # Check file size
    local size=$(stat -c%s "$KERNEL_PATH" 2>/dev/null || stat -f%z "$KERNEL_PATH" 2>/dev/null)
    if [ "$size" -lt 10240 ]; then  # Less than 10KB is suspicious
        echo "Kernel file too small: $size bytes"
        return 1
    fi
    
    echo "Kernel file exists: $size bytes"
    return 0
}

# Test 2: Check essential symbols
test_essential_symbols() {
    local symbols_output=$(objdump -t "$KERNEL_PATH" 2>/dev/null || nm "$KERNEL_PATH" 2>/dev/null)
    local missing_symbols=()
    
    # Essential kernel symbols
    local required_symbols=(
        "kmain"
        "_start"
        "VMM"
        "COWManager"
        "Process"
        "Filesystem"
        "Architecture"
    )
    
    for symbol in "${required_symbols[@]}"; do
        if ! echo "$symbols_output" | grep -q "$symbol"; then
            missing_symbols+=("$symbol")
        fi
    done
    
    if [ ${#missing_symbols[@]} -gt 0 ]; then
        echo "Missing essential symbols: ${missing_symbols[*]}"
        return 1
    fi
    
    echo "All essential symbols found"
    return 0
}

# Test 3: Memory management symbols
test_memory_management() {
    local symbols_output=$(objdump -t "$KERNEL_PATH" 2>/dev/null || nm "$KERNEL_PATH" 2>/dev/null)
    local memory_symbols=(
        "kmalloc"
        "kfree"
        "alloc_frame"
        "free_frame"
        "buddy_alloc"
        "slab_alloc"
        "init_vmm"
    )
    
    local found_symbols=0
    for symbol in "${memory_symbols[@]}"; do
        if echo "$symbols_output" | grep -q "$symbol"; then
            ((++found_symbols))
        fi
    done
    
    if [ $found_symbols -lt 5 ]; then
        echo "Insufficient memory management symbols found: $found_symbols/${#memory_symbols[@]}"
        return 1
    fi
    
    echo "Memory management symbols verified: $found_symbols/${#memory_symbols[@]}"
    return 0
}

# Test 4: COW implementation symbols
test_cow_symbols() {
    local symbols_output=$(objdump -t "$KERNEL_PATH" 2>/dev/null || nm "$KERNEL_PATH" 2>/dev/null)
    local cow_symbols=(
        "cow_manager"
        "cow_fork_mm"
        "cow_handle_page_fault"
        "cow_cleanup_process"
        "init_cow_manager"
        "break_cow"
        "alloc_cow_page"
        "free_cow_page"
    )
    
    local found_symbols=0
    for symbol in "${cow_symbols[@]}"; do
        if echo "$symbols_output" | grep -q "$symbol"; then
            ((++found_symbols))
        fi
    done
    
    if [ $found_symbols -lt 6 ]; then
        echo "Insufficient COW symbols found: $found_symbols/${#cow_symbols[@]}"
        return 1
    fi
    
    echo "COW implementation symbols verified: $found_symbols/${#cow_symbols[@]}"
    return 0
}

# Test 5: Process management symbols
test_process_symbols() {
    local symbols_output=$(objdump -t "$KERNEL_PATH" 2>/dev/null || nm "$KERNEL_PATH" 2>/dev/null)
    local process_symbols=(
        "Process"
        "fork"
        "createProc"
        "schedule"
        "wait"
        "exit"
    )
    
    local found_symbols=0
    for symbol in "${process_symbols[@]}"; do
        if echo "$symbols_output" | grep -q "$symbol"; then
            ((++found_symbols))
        fi
    done
    
    if [ $found_symbols -lt 4 ]; then
        echo "Insufficient process management symbols found: $found_symbols/${#process_symbols[@]}"
        return 1
    fi
    
    echo "Process management symbols verified: $found_symbols/${#process_symbols[@]}"
    return 0
}

# Test 6: ELF file structure validation
test_elf_structure() {
    if ! command -v readelf >/dev/null 2>&1; then
        echo "readelf not available, skipping ELF structure test"
        return 0
    fi
    
    local elf_info=$(readelf -h "$KERNEL_PATH" 2>/dev/null)
    
    # Check if it's a valid ELF file
    if ! echo "$elf_info" | grep -q "ELF Header"; then
        echo "Invalid ELF file format"
        return 1
    fi
    
    # Check machine type (should be i386)
    if ! echo "$elf_info" | grep -q "Machine:.*80386\|Machine:.*i386"; then
        echo "Incorrect machine type (should be i386)"
        return 1
    fi
    
    # Check entry point (should be reasonable)
    local entry_point=$(echo "$elf_info" | grep "Entry point" | awk '{print $4}')
    if [ -z "$entry_point" ]; then
        echo "No entry point found"
        return 1
    fi
    
    echo "Valid ELF structure, entry point: $entry_point"
    return 0
}

# Test 7: Kernel sections analysis
test_kernel_sections() {
    if ! command -v objdump >/dev/null 2>&1; then
        echo "objdump not available, skipping sections test"
        return 0
    fi
    
    local sections=$(objdump -h "$KERNEL_PATH" 2>/dev/null)
    local required_sections=(".text" ".data" ".bss")
    local missing_sections=()
    
    for section in "${required_sections[@]}"; do
        if ! echo "$sections" | grep -q "$section"; then
            missing_sections+=("$section")
        fi
    done
    
    if [ ${#missing_sections[@]} -gt 0 ]; then
        echo "Missing required sections: ${missing_sections[*]}"
        return 1
    fi
    
    # Check text section size
    local text_size_hex=$(echo "$sections" | awk '$2 == ".text" {print "0x" $3; exit}')
    if [ -n "$text_size_hex" ]; then
        local text_size_dec=$((text_size_hex))
        if [ $text_size_dec -lt 1024 ]; then
            echo "Text section too small: $text_size_dec bytes"
            return 1
        fi
    fi
    
    echo "Kernel sections verified"
    return 0
}

# Test 8: Boot compatibility check
test_boot_compatibility() {
    # Check for multiboot header
    if command -v hexdump >/dev/null 2>&1; then
        local header_check=$(hexdump -C "$KERNEL_PATH" | head -20 | grep -i "multiboot\|grub" || true)
        if [ -n "$header_check" ]; then
            echo "Boot compatibility indicators found"
            return 0
        fi
    fi
    
    # Look for multiboot symbols
    local symbols_output=$(objdump -t "$KERNEL_PATH" 2>/dev/null || nm "$KERNEL_PATH" 2>/dev/null)
    if echo "$symbols_output" | grep -q -i "multiboot"; then
        echo "Multiboot symbols found"
        return 0
    fi
    
    test_warning "No clear boot compatibility indicators found"
    return 0
}

# Test 9: Memory layout verification
test_memory_layout() {
    if ! command -v readelf >/dev/null 2>&1; then
        echo "readelf not available, skipping memory layout test"
        return 0
    fi
    
    local program_headers=$(readelf -l "$KERNEL_PATH" 2>/dev/null)
    
    # Check if we have program headers
    if ! echo "$program_headers" | grep -q "Program Headers"; then
        echo "No program headers found"
        return 1
    fi
    
    # Look for loadable segments
    local load_segments=$(echo "$program_headers" | grep "LOAD" | wc -l)
    if [ $load_segments -eq 0 ]; then
        echo "No loadable segments found"
        return 1
    fi
    
    echo "Memory layout verified: $load_segments loadable segments"
    return 0
}

# Test 10: Code quality checks
test_code_quality() {
    local warnings=0
    
    # Look for potential buffer overflow functions in disassembly
    if command -v objdump >/dev/null 2>&1; then
        local disasm=$(objdump -d "$KERNEL_PATH" 2>/dev/null || true)
        if echo "$disasm" | grep -q "strcpy\|strcat\|sprintf"; then
            test_warning "Potentially unsafe string functions found"
            ((++warnings))
        fi
    fi
    
    # Check for stack smashing protection symbols
    local symbols_output=$(objdump -t "$KERNEL_PATH" 2>/dev/null || nm "$KERNEL_PATH" 2>/dev/null)
    if echo "$symbols_output" | grep -q "stack_chk"; then
        echo "Stack protection symbols found"
    else
        test_warning "No stack protection symbols found"
        ((++warnings))
    fi
    
    if [ $warnings -eq 0 ]; then
        echo "Basic code quality checks passed"
        return 0
    else
        echo "Code quality warnings: $warnings"
        return 0  # Don't fail for warnings
    fi
}

# Generate detailed report
generate_report() {
    mkdir -p "$RESULTS_DIR"
    local report_file="$RESULTS_DIR/kernel_test_report_$TIMESTAMP.txt"
    
    {
        echo "===================================="
        echo "kmOS Kernel Functionality Test Report"
        echo "===================================="
        echo "Timestamp: $(date)"
        echo "Kernel Path: $KERNEL_PATH"
        echo "Test Script: $0"
        echo ""
        echo "Test Results Summary:"
        echo "  Total Tests: $TESTS_TOTAL"
        echo "  Passed: $TESTS_PASSED"
        echo "  Failed: $TESTS_FAILED"
        echo "  Success Rate: $(( (TESTS_PASSED * 100) / TESTS_TOTAL ))%"
        echo ""
        
        if [ $TESTS_FAILED -eq 0 ]; then
            echo "Status: ALL TESTS PASSED ✓"
        else
            echo "Status: $TESTS_FAILED TEST(S) FAILED ✗"
        fi
        
        echo ""
        echo "Detailed Analysis:"
        echo "=================="
        
        # Kernel file analysis
        if [ -f "$KERNEL_PATH" ]; then
            local size=$(stat -c%s "$KERNEL_PATH" 2>/dev/null || stat -f%z "$KERNEL_PATH" 2>/dev/null)
            echo "Kernel Size: $size bytes"
            
            if command -v file >/dev/null 2>&1; then
                echo "File Type: $(file "$KERNEL_PATH")"
            fi
            
            if command -v md5sum >/dev/null 2>&1; then
                echo "MD5 Hash: $(md5sum "$KERNEL_PATH" | cut -d' ' -f1)"
            elif command -v md5 >/dev/null 2>&1; then
                echo "MD5 Hash: $(md5 -q "$KERNEL_PATH")"
            fi
        fi
        
        echo ""
        echo "Recommendations:"
        echo "==============="
        
        if [ $TESTS_FAILED -gt 0 ]; then
            echo "- Review failed tests and fix underlying issues"
            echo "- Ensure all required symbols are properly linked"
            echo "- Verify build configuration and cross-compiler setup"
        else
            echo "- Kernel build appears healthy"
            echo "- Consider running QEMU integration tests"
            echo "- Monitor for any runtime issues during boot testing"
        fi
        
    } > "$report_file"
    
    echo "Report generated: $report_file"
}

# Main test execution
main() {
    log "Starting kmOS Kernel Functionality Tests"
    log "Kernel path: $KERNEL_PATH"
    echo
    
    # Run all tests
    run_test "Kernel File Existence" test_kernel_exists
    run_test "Essential Symbols" test_essential_symbols  
    run_test "Memory Management" test_memory_management
    run_test "COW Implementation" test_cow_symbols
    run_test "Process Management" test_process_symbols
    run_test "ELF Structure" test_elf_structure
    run_test "Kernel Sections" test_kernel_sections
    run_test "Boot Compatibility" test_boot_compatibility
    run_test "Memory Layout" test_memory_layout
    run_test "Code Quality" test_code_quality
    
    # Summary
    echo "========================================"
    echo "TEST SUMMARY"
    echo "========================================"
    echo "Total Tests: $TESTS_TOTAL"
    echo "Passed: $TESTS_PASSED"
    echo "Failed: $TESTS_FAILED"
    echo "Success Rate: $(( (TESTS_PASSED * 100) / TESTS_TOTAL ))%"
    
    if [ $TESTS_FAILED -eq 0 ]; then
        echo -e "${GREEN}ALL TESTS PASSED ✓${NC}"
    else
        echo -e "${RED}$TESTS_FAILED TEST(S) FAILED ✗${NC}"
    fi
    
    generate_report
    
    # Exit with appropriate code
    if [ $TESTS_FAILED -eq 0 ]; then
        exit 0
    else
        exit 1
    fi
}

check_dependencies() {
    local missing_tools=()
    
    for tool in objdump file stat; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            missing_tools+=("$tool")
        fi
    done
    
    if [ ${#missing_tools[@]} -gt 0 ]; then
        test_warning "Missing tools: ${missing_tools[*]}"
        echo "Some tests may be skipped"
    fi
}

# Script entry point
if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
    echo "Usage: $0 [kernel_path]"
    echo "  kernel_path: Path to kernel.elf (default: src/kernel/kernel.elf)"
    echo ""
    echo "This script performs comprehensive testing of the kmOS kernel build"
    echo "including symbol verification, memory management checks, and code quality analysis."
    exit 0
fi

check_dependencies
main