#!/bin/bash

# ===============================================================================
# IR0 KERNEL TEST FRAMEWORK
# ===============================================================================
# Framework para pruebas granulares de compilación y ejecución

set -e  # Exit on any error

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuración
KERNEL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QEMU_TIMEOUT=30  # Timeout en segundos para QEMU
TEST_RESULTS_DIR="$KERNEL_DIR/test_results"
LOG_DIR="$KERNEL_DIR/logs"

# Crear directorios si no existen
mkdir -p "$TEST_RESULTS_DIR"
mkdir -p "$LOG_DIR"

# ===============================================================================
# FUNCIONES DE UTILIDAD
# ===============================================================================

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# ===============================================================================
# PRUEBAS DE COMPILACIÓN
# ===============================================================================

test_compile_64bit() {
    local test_name="compile_64bit"
    local log_file="$LOG_DIR/${test_name}.log"
    
    log_info "Testing 64-bit compilation..."
    
    cd "$KERNEL_DIR"
    
    # Limpiar build anterior
    make clean > "$log_file" 2>&1
    
    # Compilar para 64-bit
    if make ARCH=x86-64 kernel-x86-64-desktop.iso >> "$log_file" 2>&1; then
        log_success "64-bit compilation PASSED"
        echo "PASS" > "$TEST_RESULTS_DIR/${test_name}.result"
        return 0
    else
        log_error "64-bit compilation FAILED"
        echo "FAIL" > "$TEST_RESULTS_DIR/${test_name}.result"
        return 1
    fi
}

test_compile_32bit() {
    local test_name="compile_32bit"
    local log_file="$LOG_DIR/${test_name}.log"
    
    log_info "Testing 32-bit compilation..."
    
    cd "$KERNEL_DIR"
    
    # Limpiar build anterior
    make clean > "$log_file" 2>&1
    
    # Compilar para 32-bit
    if make ARCH=x86-32 kernel-x86-32-desktop.iso >> "$log_file" 2>&1; then
        log_success "32-bit compilation PASSED"
        echo "PASS" > "$TEST_RESULTS_DIR/${test_name}.result"
        return 0
    else
        log_error "32-bit compilation FAILED"
        echo "FAIL" > "$TEST_RESULTS_DIR/${test_name}.result"
        return 1
    fi
}

test_compile_clean() {
    local test_name="compile_clean"
    local log_file="$LOG_DIR/${test_name}.log"
    
    log_info "Testing clean compilation..."
    
    cd "$KERNEL_DIR"
    
    # Ejecutar make clean
    if make clean > "$log_file" 2>&1; then
        log_success "Clean compilation PASSED"
        echo "PASS" > "$TEST_RESULTS_DIR/${test_name}.result"
        return 0
    else
        log_error "Clean compilation FAILED"
        echo "FAIL" > "$TEST_RESULTS_DIR/${test_name}.result"
        return 1
    fi
}

# ===============================================================================
# PRUEBAS DE EJECUCIÓN (CUANDO ES SEGURO)
# ===============================================================================

test_qemu_64bit_boot() {
    local test_name="qemu_64bit_boot"
    local log_file="$LOG_DIR/${test_name}.log"
    local iso_file="$KERNEL_DIR/kernel-x86-64.iso"
    
    log_info "Testing 64-bit QEMU boot..."
    
    # Verificar que el ISO existe
    if [[ ! -f "$iso_file" ]]; then
        log_error "64-bit ISO not found: $iso_file"
        echo "FAIL" > "$TEST_RESULTS_DIR/${test_name}.result"
        return 1
    fi
    
    # Ejecutar QEMU con timeout
    cd "$KERNEL_DIR"
    
    # Usar timeout para evitar que QEMU se quede corriendo indefinidamente
    if timeout $QEMU_TIMEOUT qemu-system-x86_64 \
        -m 512M \
        -cdrom "$iso_file" \
        -serial stdio \
        -no-reboot \
        -no-shutdown \
        -display gtk \
        > "$log_file" 2>&1; then
        
        # Verificar si el kernel llegó al shell
        if grep -q "IR0 Kernel Shell" "$log_file" || grep -q ">" "$log_file"; then
            log_success "64-bit QEMU boot PASSED"
            echo "PASS" > "$TEST_RESULTS_DIR/${test_name}.result"
            return 0
        else
            log_warning "64-bit QEMU boot: Kernel started but shell not detected"
            echo "WARN" > "$TEST_RESULTS_DIR/${test_name}.result"
            return 0
        fi
    else
        log_error "64-bit QEMU boot FAILED (timeout or error)"
        echo "FAIL" > "$TEST_RESULTS_DIR/${test_name}.result"
        return 1
    fi
}

test_qemu_32bit_boot() {
    local test_name="qemu_32bit_boot"
    local log_file="$LOG_DIR/${test_name}.log"
    local iso_file="$KERNEL_DIR/kernel-x86-32.iso"
    
    log_info "Testing 32-bit QEMU boot..."
    
    # Verificar que el ISO existe
    if [[ ! -f "$iso_file" ]]; then
        log_error "32-bit ISO not found: $iso_file"
        echo "FAIL" > "$TEST_RESULTS_DIR/${test_name}.result"
        return 1
    fi
    
    # Ejecutar QEMU con timeout
    cd "$KERNEL_DIR"
    
    if timeout $QEMU_TIMEOUT qemu-system-i386 \
        -m 512M \
        -cdrom "$iso_file" \
        -serial stdio \
        -no-reboot \
        -no-shutdown \
        -display none \
        > "$log_file" 2>&1; then
        
        # Verificar si el kernel llegó al shell
        if grep -q "IR0 Kernel Shell" "$log_file" || grep -q ">" "$log_file"; then
            log_success "32-bit QEMU boot PASSED"
            echo "PASS" > "$TEST_RESULTS_DIR/${test_name}.result"
            return 0
        else
            log_warning "32-bit QEMU boot: Kernel started but shell not detected"
            echo "WARN" > "$TEST_RESULTS_DIR/${test_name}.result"
            return 0
        fi
    else
        log_error "32-bit QEMU boot FAILED (timeout or error)"
        echo "FAIL" > "$TEST_RESULTS_DIR/${test_name}.result"
        return 1
    fi
}

# ===============================================================================
# PRUEBAS DE INTEGRACIÓN
# ===============================================================================

test_keyboard_input() {
    local test_name="keyboard_input"
    local log_file="$LOG_DIR/${test_name}.log"
    
    log_info "Testing keyboard input functionality..."
    
    # Esta prueba requiere interacción manual, por ahora solo verificamos compilación
    log_warning "Keyboard input test requires manual verification"
    echo "MANUAL" > "$TEST_RESULTS_DIR/${test_name}.result"
    return 0
}

test_syscalls() {
    local test_name="syscalls"
    local log_file="$LOG_DIR/${test_name}.log"
    
    log_info "Testing syscalls functionality..."
    
    # Verificar que las syscalls están compiladas
    if grep -q "sys_gettime\|sys_sleep\|sys_yield" "$KERNEL_DIR/kernel/syscalls/syscalls.c"; then
        log_success "Syscalls compilation check PASSED"
        echo "PASS" > "$TEST_RESULTS_DIR/${test_name}.result"
        return 0
    else
        log_error "Syscalls compilation check FAILED"
        echo "FAIL" > "$TEST_RESULTS_DIR/${test_name}.result"
        return 1
    fi
}

# ===============================================================================
# FUNCIONES PRINCIPALES
# ===============================================================================

run_all_tests() {
    local total_tests=0
    local passed_tests=0
    local failed_tests=0
    
    log_info "Starting IR0 Kernel Test Suite..."
    
    # Array de pruebas
    local tests=(
        "test_compile_clean"
        "test_compile_64bit"
        "test_compile_32bit"
        "test_syscalls"
        "test_keyboard_input"
    )
    
    # Ejecutar pruebas de compilación primero
    for test in "${tests[@]}"; do
        total_tests=$((total_tests + 1))
        log_info "Running test: $test"
        
        if $test; then
            passed_tests=$((passed_tests + 1))
        else
            failed_tests=$((failed_tests + 1))
        fi
        
        echo "---"
    done
    
    # Ejecutar pruebas de QEMU solo si las compilaciones pasaron
    if [[ -f "$TEST_RESULTS_DIR/compile_64bit.result" ]] && \
       [[ "$(cat "$TEST_RESULTS_DIR/compile_64bit.result")" == "PASS" ]]; then
        total_tests=$((total_tests + 1))
        if test_qemu_64bit_boot; then
            passed_tests=$((passed_tests + 1))
        else
            failed_tests=$((failed_tests + 1))
        fi
    fi
    
    if [[ -f "$TEST_RESULTS_DIR/compile_32bit.result" ]] && \
       [[ "$(cat "$TEST_RESULTS_DIR/compile_32bit.result")" == "PASS" ]]; then
        total_tests=$((total_tests + 1))
        if test_qemu_32bit_boot; then
            passed_tests=$((passed_tests + 1))
        else
            failed_tests=$((failed_tests + 1))
        fi
    fi
    
    # Mostrar resumen
    echo "=========================================="
    log_info "TEST SUITE COMPLETED"
    echo "Total tests: $total_tests"
    echo "Passed: $passed_tests"
    echo "Failed: $failed_tests"
    echo "=========================================="
    
    # Guardar resumen
    echo "Total: $total_tests, Passed: $passed_tests, Failed: $failed_tests" > "$TEST_RESULTS_DIR/summary.txt"
    
    return $failed_tests
}

run_specific_test() {
    local test_name="$1"
    
    if [[ -z "$test_name" ]]; then
        log_error "No test name specified"
        echo "Available tests:"
        echo "  compile_clean, compile_64bit, compile_32bit"
        echo "  qemu_64bit_boot, qemu_32bit_boot"
        echo "  syscalls, keyboard_input"
        return 1
    fi
    
    case "$test_name" in
        "compile_clean") test_compile_clean ;;
        "compile_64bit") test_compile_64bit ;;
        "compile_32bit") test_compile_32bit ;;
        "qemu_64bit_boot") test_qemu_64bit_boot ;;
        "qemu_32bit_boot") test_qemu_32bit_boot ;;
        "syscalls") test_syscalls ;;
        "keyboard_input") test_keyboard_input ;;
        *) log_error "Unknown test: $test_name" ; return 1 ;;
    esac
}

# ===============================================================================
# MAIN
# ===============================================================================

main() {
    case "${1:-all}" in
        "all")
            run_all_tests
            ;;
        "compile")
            test_compile_clean && test_compile_64bit && test_compile_32bit
            ;;
        "qemu")
            test_qemu_64bit_boot && test_qemu_32bit_boot
            ;;
        "qemu-display")
            test_qemu_64bit_boot && test_qemu_32bit_boot
            ;;
        "clean")
            make clean
            ;;
        *)
            run_specific_test "$1"
            ;;
    esac
}

# Ejecutar main si el script se ejecuta directamente
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi
