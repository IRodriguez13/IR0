#!/bin/bash
# test_ir0_complete.sh - Testing completo del kernel IR0

echo "╔══════════════════════════════════════════════════════╗"
echo "║              IR0 KERNEL - TEST COMPLETO              ║"
echo "╚══════════════════════════════════════════════════════╝"

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Función de logging
log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[OK]${NC} $1"; }
log_warning() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Verificar dependencias
check_deps() 
{
    log_info "Verificando dependencias..."
    
    if ! command -v gcc &> /dev/null; then
        log_error "gcc no encontrado"
        exit 1
    fi
    
    if ! command -v nasm &> /dev/null; then
        log_error "nasm no encontrado"
        exit 1
    fi
    
    if ! command -v ld &> /dev/null; then
        log_error "ld no encontrado"
        exit 1
    fi
    
    log_success "Dependencias verificadas"
}

# Limpiar archivos compilados
clean_build_files() 
{
    log_info "Limpiando archivos de compilación..."
    
    # Limpiar archivos .o y .d
    find . -name "*.o" -type f -delete
    find . -name "*.d" -type f -delete
    
    # Limpiar archivos binarios
    rm -f *.bin *.iso *.elf
    
    # Limpiar directorios iso
    rm -rf iso-*
    
    log_success "Limpieza completada"
}

# Test de subsistemas por arquitectura
test_subsystems() 
{
    local arch=$1
    log_info "Testing subsistemas para arquitectura: $arch"
    
    # Limpiar primero
    make ARCH=$arch clean > /dev/null 2>&1
    
    # Compilar solo subsistemas
    if make ARCH=$arch subsystems > build_subsystems_$arch.log 2>&1; then
        log_success "$arch: Subsistemas compilados exitosamente"
        return 0
    else
        log_error "$arch: Compilación de subsistemas FALLÓ"
        log_error "Ver build_subsystems_$arch.log para detalles"
        return 1
    fi
}

# Test de compilación completa por arquitectura
test_full_build() 
{
    local arch=$1
    log_info "Testing compilación completa para arquitectura: $arch"
    
    # Limpiar solo para esta arquitectura
    make ARCH=$arch clean > /dev/null 2>&1
    
    # Compilar todo
    if make ARCH=$arch all > build_full_$arch.log 2>&1; then
        log_success "$arch: Compilación completa exitosa"
        
        # Verificar archivos generados
        if [[ -f "kernel-$arch.bin" ]]; then
            local size=$(stat -c%s "kernel-$arch.bin" 2>/dev/null || stat -f%z "kernel-$arch.bin")
            log_success "$arch: kernel-$arch.bin generado (${size} bytes)"
        else
            log_error "$arch: kernel-$arch.bin NO encontrado"
            return 1
        fi
        
        # Verificar ISO
        if [[ -f "kernel-$arch.iso" ]]; then
            log_success "$arch: kernel-$arch.iso generado"
        else
            log_warning "$arch: kernel-$arch.iso no generado (grub-mkrescue faltante?)"
        fi
        
        return 0
    else
        log_error "$arch: Compilación completa FALLÓ"
        log_error "Ver build_full_$arch.log para detalles"
        return 1
    fi
}

# Verificar estructura de archivos
check_structure() 
{
    log_info "Verificando estructura del proyecto..."
    
    local critical_files=(
        "Makefile"
        "arch/common/arch_interface.h"
        "includes/stdint.h"
        "includes/stddef.h"
        "memory/memo_interface.h"
        "kernel/kernel_start.c"
    )
    
    for file in "${critical_files[@]}"; do
        if [[ -f "$file" ]]; then
            log_success "✓ $file"
        else
            log_error "✗ $file FALTANTE"
            return 1
        fi
    done
    
    return 0
}

# Test de subsistemas individuales
test_individual_subsystems() 
{
    log_info "Verificando subsistemas individuales..."
    
    # Test subsistema de memoria
    if [[ -f "memory/Makefile" ]]; then
        log_success "✓ Subsistema de memoria"
    else
        log_error "✗ Subsistema de memoria"
    fi
    
    # Test subsistema de timers
    if [[ -f "drivers/timer/clock_system.c" ]]; then
        log_success "✓ Subsistema de timers"
    else
        log_error "✗ Subsistema de timers"
    fi
    
    # Test subsistema de interrupciones
    if [[ -f "interrupt/idt.c" ]]; then
        log_success "✓ Subsistema de interrupciones"
    else
        log_error "✗ Subsistema de interrupciones"
    fi
    
    # Test scheduler
    if [[ -f "kernel/scheduler/scheduler.h" ]]; then
        log_success "✓ Scheduler"
    else
        log_error "✗ Scheduler"
    fi
}

# Main test sequence
main() 
{
    check_deps
    
    if ! check_structure; then
        log_error "Estructura de proyecto inválida"
        exit 1
    fi
    
    test_individual_subsystems
    
    # Limpiar antes de empezar
    clean_build_files
    
    # Test subsistemas por arquitectura
    log_info "Iniciando tests de subsistemas..."
    
    local subsystems_success_count=0
    local total_count=0
    
    for arch in "x86-32" "x86-64"; do
        ((total_count++))
        if test_subsystems "$arch"; then
            ((subsystems_success_count++))
        fi
    done
    
    # Test compilación completa por arquitectura
    log_info "Iniciando tests de compilación completa..."
    
    local full_success_count=0
    
    for arch in "x86-32" "x86-64"; do
        ((total_count++))
        if test_full_build "$arch"; then
            ((full_success_count++))
        fi
    done
    
    # Resultados finales
    echo
    echo "╔══════════════════════════════════════════════════════╗"
    local total_success=$((subsystems_success_count + full_success_count))
    local total_tests=$((total_count))
    
    if [[ $total_success -eq $total_tests ]]; then
        log_success "TODOS LOS TESTS PASARON ($total_success/$total_tests)"
        echo "║                   🎉 LISTO PARA QEMU                ║"
        echo "╚══════════════════════════════════════════════════════╝"
        echo
        echo "Para ejecutar:"
        echo "  make ARCH=x86-32 run    # Test 32-bit"
        echo "  make ARCH=x86-64 run    # Test 64-bit"
        echo
        echo "Para limpiar:"
        echo "  make clean-all          # Limpiar todas las arquitecturas"
        exit 0
    else
        log_error "ALGUNOS TESTS FALLARON ($total_success/$total_tests)"
        echo "║               ❌ REVISAR ERRORES                    ║"
        echo "╚══════════════════════════════════════════════════════╝"
        echo
        echo "Logs disponibles:"
        echo "  build_subsystems_x86-32.log"
        echo "  build_subsystems_x86-64.log"
        echo "  build_full_x86-32.log"
        echo "  build_full_x86-64.log"
        exit 1
    fi
}

main "$@"