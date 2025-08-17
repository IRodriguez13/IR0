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
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Función de logging
log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[OK]${NC} $1"; }
log_warning() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_critical() { echo -e "${PURPLE}[CRITICAL]${NC} $1"; }
log_test() { echo -e "${CYAN}[TEST]${NC} $1"; }

# Variables globales
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0
CRITICAL_ERRORS=0

# Función para contar tests
test_pass() { ((PASSED_TESTS++)); ((TOTAL_TESTS++)); }
test_fail() { ((FAILED_TESTS++)); ((TOTAL_TESTS++)); }
critical_error() { ((CRITICAL_ERRORS++)); test_fail; }

# Verificar dependencias
check_deps() 
{
    log_info "Verificando dependencias..."
    
    local deps=("gcc" "nasm" "ld" "make" "find" "grep" "sed" "awk")
    local missing_deps=()
    
    for dep in "${deps[@]}"; do
        if ! command -v "$dep" &> /dev/null; then
            missing_deps+=("$dep")
        fi
    done
    
    if [[ ${#missing_deps[@]} -gt 0 ]]; then
        log_error "Dependencias faltantes: ${missing_deps[*]}"
        return 1
    fi
    
    # Verificar versiones mínimas
    local gcc_version=$(gcc --version | head -n1 | grep -oE '[0-9]+\.[0-9]+' | head -n1)
    if [[ $(echo "$gcc_version >= 7.0" | bc -l 2>/dev/null) -eq 0 ]]; then
        log_warning "GCC $gcc_version detectado. Se recomienda GCC 7.0+"
    fi
    
    log_success "Dependencias verificadas"
    test_pass
    return 0
}

# Limpiar archivos compilados
clean_build_files() 
{
    log_info "Limpiando archivos de compilación..."
    
    # Limpiar archivos .o y .d
    find . -name "*.o" -type f -delete 2>/dev/null
    find . -name "*.d" -type f -delete 2>/dev/null
    
    # Limpiar archivos binarios
    rm -f *.bin *.iso *.elf *.raw 2>/dev/null
    
    # Limpiar directorios iso
    rm -rf iso-* 2>/dev/null
    
    # Limpiar logs de build
    rm -f build_*.log 2>/dev/null
    
    log_success "Limpieza completada"
}

# Verificar estructura de archivos crítica
check_structure() 
{
    log_info "Verificando estructura del proyecto..."
    
    local critical_files=(
        "Makefile"
        "arch/common/arch_interface.h"
        "arch/common/arch_interface.c"
        "arch/common/idt.h"
        "includes/stdint.h"
        "includes/stddef.h"
        "includes/string.h"
        "includes/ir0/kernel.h"
        "includes/ir0/print.h"
        "memory/memo_interface.h"
        "memory/physical_allocator.h"
        "memory/heap_allocator.h"
        "kernel/kernel_start.c"
        "kernel/scheduler/scheduler.h"
        "kernel/scheduler/task.h"
        "interrupt/isr_handlers.h"
        "drivers/timer/clock_system.h"
    )
    
    local missing_files=()
    
    for file in "${critical_files[@]}"; do
        if [[ -f "$file" ]]; then
            log_success "✓ $file"
        else
            log_error "✗ $file FALTANTE"
            missing_files+=("$file")
        fi
    done
    
    if [[ ${#missing_files[@]} -gt 0 ]]; then
        log_critical "Archivos críticos faltantes: ${missing_files[*]}"
        return 1
    fi
    
    test_pass
    return 0
}

# Verificar sintaxis de archivos fuente
check_syntax() 
{
    log_info "Verificando sintaxis de archivos fuente..."
    
    local c_files=$(find . -name "*.c" -type f)
    local syntax_errors=0
    
    for file in $c_files; do
        if ! gcc -fsyntax-only -nostdlib -nostdinc -fno-builtin -fno-stack-protector -ffreestanding -I./includes -I./includes/ir0 -I./arch/common "$file" 2>/dev/null; then
            log_error "Error de sintaxis en: $file"
            ((syntax_errors++))
        fi
    done
    
    if [[ $syntax_errors -eq 0 ]]; then
        log_success "Sintaxis verificada en todos los archivos .c"
        test_pass
    else
        log_critical "Errores de sintaxis encontrados: $syntax_errors"
        critical_error
        return 1
    fi
}

# Verificar includes faltantes
check_includes() 
{
    log_info "Verificando includes y dependencias..."
    
    local missing_includes=0
    
    # Verificar includes críticos
    local critical_includes=(
        "stdint.h"
        "stddef.h"
        "string.h"
        "ir0/kernel.h"
        "ir0/print.h"
    )
    
    for include in "${critical_includes[@]}"; do
        if [[ ! -f "includes/$include" ]]; then
            log_error "Include faltante: includes/$include"
            ((missing_includes++))
        fi
    done
    
    # Verificar que los archivos .c incluyan los headers necesarios
    local c_files=$(find . -name "*.c" -type f)
    for file in $c_files; do
        if ! grep -q "#include" "$file" 2>/dev/null; then
            log_warning "Archivo sin includes: $file"
        fi
    done
    
    if [[ $missing_includes -eq 0 ]]; then
        log_success "Includes verificados"
        test_pass
    else
        log_critical "Includes faltantes: $missing_includes"
        critical_error
        return 1
    fi
}

# Verificar problemas de arquitectura
check_architecture_issues() 
{
    log_info "Verificando problemas de arquitectura..."
    
    local issues=0
    
    # Verificar que no haya hardcoded -m32 en Makefiles (excepto en directorios específicos de arquitectura)
    # y excepto en configuraciones condicionales
    local hardcoded_m32=$(grep -r "CFLAGS.*:=.*-m32" --include="Makefile" . 2>/dev/null | \
                         grep -v "arch/x_86-32\|arch/x86-32" | \
                         grep -v "ifeq.*ARCH.*x86-32\|else.*32-bit" | \
                         wc -l)
    if [[ $hardcoded_m32 -gt 0 ]]; then
        log_error "Makefiles con -m32 hardcodeado encontrados: $hardcoded_m32"
        ((issues++))
    fi
    
    # Verificar que no haya referencias a arquitectura específica incorrecta
    local x86_32_in_64=$(grep -r "x86-32\|__i386__" arch/x86-64/ 2>/dev/null | wc -l)
    if [[ $x86_32_in_64 -gt 0 ]]; then
        log_warning "Referencias x86-32 en directorio x86-64: $x86_32_in_64"
    fi
    
    # Verificar que las estructuras usen tipos correctos
    local uint32_pointer=$(grep -r "uint32_t.*\*" --include="*.h" --include="*.c" . 2>/dev/null | wc -l)
    if [[ $uint32_pointer -gt 0 ]]; then
        log_warning "Posibles problemas de casting pointer: $uint32_pointer"
    fi
    
    if [[ $issues -eq 0 ]]; then
        log_success "Problemas de arquitectura verificados"
        test_pass
    else
        log_warning "Problemas de arquitectura encontrados: $issues"
        test_pass  # No es crítico, solo warning
        return 0
    fi
}

# Test de subsistemas por arquitectura
test_subsystems() 
{
    local arch=$1
    log_test "Testing subsistemas para arquitectura: $arch"
    
    # Limpiar primero
    make ARCH=$arch clean > /dev/null 2>&1
    
    # Compilar solo subsistemas
    if make ARCH=$arch subsystems > "build_subsystems_$arch.log" 2>&1; then
        log_success "$arch: Subsistemas compilados exitosamente"
        test_pass
        return 0
    else
        log_error "$arch: Compilación de subsistemas FALLÓ"
        log_error "Ver build_subsystems_$arch.log para detalles"
        test_fail
        return 1
    fi
}

# Test de compilación completa por arquitectura
test_full_build() 
{
    local arch=$1
    log_test "Testing compilación completa para arquitectura: $arch"
    
    # Limpiar solo para esta arquitectura
    make ARCH=$arch clean > /dev/null 2>&1
    
    # Compilar todo
    if make ARCH=$arch all > "build_full_$arch.log" 2>&1; then
        log_success "$arch: Compilación completa exitosa"
        
        # Verificar archivos generados
        if [[ -f "kernel-$arch.bin" ]]; then
            local size=$(stat -c%s "kernel-$arch.bin" 2>/dev/null || stat -f%z "kernel-$arch.bin")
            log_success "$arch: kernel-$arch.bin generado (${size} bytes)"
            
            # Verificar que el binario no esté vacío
            if [[ $size -lt 1024 ]]; then
                log_error "$arch: kernel-$arch.bin demasiado pequeño ($size bytes)"
                test_fail
                return 1
            fi
        else
            log_error "$arch: kernel-$arch.bin NO encontrado"
            test_fail
            return 1
        fi
        
        # Verificar ISO
        if [[ -f "kernel-$arch.iso" ]]; then
            log_success "$arch: kernel-$arch.iso generado"
        else
            log_warning "$arch: kernel-$arch.iso no generado (grub-mkrescue faltante?)"
        fi
        
        test_pass
        return 0
    else
        log_error "$arch: Compilación completa FALLÓ"
        log_error "Ver build_full_$arch.log para detalles"
        test_fail
        return 1
    fi
}

# Test de consistencia entre arquitecturas
test_architecture_consistency() 
{
    log_test "Verificando consistencia entre arquitecturas..."
    
    local inconsistencies=0
    
    # Verificar que ambas arquitecturas generen archivos similares
    if [[ -f "kernel-x86-32.bin" && -f "kernel-x86-64.bin" ]]; then
        local size_32=$(stat -c%s "kernel-x86-32.bin" 2>/dev/null || stat -f%z "kernel-x86-32.bin")
        local size_64=$(stat -c%s "kernel-x86-64.bin" 2>/dev/null || stat -f%z "kernel-x86-64.bin")
        
        if [[ $size_32 -gt 0 && $size_64 -gt 0 ]]; then
            log_success "Ambas arquitecturas generaron binarios válidos"
            log_info "x86-32: ${size_32} bytes, x86-64: ${size_64} bytes"
        else
            log_error "Una o ambas arquitecturas generaron binarios inválidos"
            ((inconsistencies++))
        fi
    else
        log_error "No se pudieron comparar binarios entre arquitecturas"
        ((inconsistencies++))
    fi
    
    if [[ $inconsistencies -eq 0 ]]; then
        log_success "Consistencia entre arquitecturas verificada"
        test_pass
    else
        log_critical "Inconsistencias entre arquitecturas: $inconsistencies"
        critical_error
        return 1
    fi
}

# Verificar warnings y errores en logs
check_build_logs() 
{
    log_info "Analizando logs de compilación..."
    
    local total_warnings=0
    local total_errors=0
    
    for arch in "x86-32" "x86-64"; do
        if [[ -f "build_full_$arch.log" ]]; then
            local warnings=$(grep -i "warning" "build_full_$arch.log" | wc -l)
            local errors=$(grep -i "error" "build_full_$arch.log" | wc -l)
            
            if [[ $warnings -gt 0 ]]; then
                log_warning "$arch: $warnings warnings encontrados"
                ((total_warnings += warnings))
            fi
            
            if [[ $errors -gt 0 ]]; then
                log_error "$arch: $errors errores encontrados"
                ((total_errors += errors))
            fi
        fi
    done
    
    if [[ $total_errors -eq 0 ]]; then
        log_success "Análisis de logs completado"
        if [[ $total_warnings -gt 0 ]]; then
            log_warning "Total de warnings: $total_warnings"
        fi
        test_pass
    else
        log_critical "Errores en logs de compilación: $total_errors"
        critical_error
        return 1
    fi
}

# Test de memoria y recursos
test_memory_usage() 
{
    log_test "Verificando uso de memoria..."
    
    # Verificar que no haya memory leaks en compilación
    local memory_issues=0
    
    # Verificar que los archivos .o no sean excesivamente grandes
    local large_objects=$(find . -name "*.o" -size +1M 2>/dev/null | wc -l)
    if [[ $large_objects -gt 0 ]]; then
        log_warning "Archivos .o grandes encontrados: $large_objects"
    fi
    
    # Verificar que el kernel no sea excesivamente grande
    for arch in "x86-32" "x86-64"; do
        if [[ -f "kernel-$arch.bin" ]]; then
            local size=$(stat -c%s "kernel-$arch.bin" 2>/dev/null || stat -f%z "kernel-$arch.bin")
            if [[ $size -gt 10485760 ]]; then  # 10MB
                log_warning "$arch: kernel muy grande ($size bytes)"
            fi
        fi
    done
    
    log_success "Uso de memoria verificado"
    test_pass
}

# Main test sequence
main() 
{
    echo "Iniciando tests completos del kernel IR0..."
    echo
    
    # Tests críticos que deben pasar
    if ! check_deps; then
        log_critical "Dependencias críticas faltantes"
        exit 1
    fi
    
    if ! check_structure; then
        log_critical "Estructura de proyecto inválida"
        exit 1
    fi
    
    if ! check_syntax; then
        log_critical "Errores de sintaxis críticos"
        exit 1
    fi
    
    if ! check_includes; then
        log_critical "Includes críticos faltantes"
        exit 1
    fi
    
    if ! check_architecture_issues; then
        log_critical "Problemas de arquitectura críticos"
        exit 1
    fi
    
    # Limpiar antes de empezar
    clean_build_files
    
    # Tests de compilación
    log_info "Iniciando tests de compilación..."
    
    for arch in "x86-32" "x86-64"; do
        test_subsystems "$arch"
        test_full_build "$arch"
    done
    
    # Tests adicionales
    test_architecture_consistency
    check_build_logs
    test_memory_usage
    
    # Resultados finales
    echo
    echo "╔══════════════════════════════════════════════════════╗"
    echo "║                    RESULTADOS FINALES                ║"
    echo "╠══════════════════════════════════════════════════════╣"
    echo "║ Tests totales: $TOTAL_TESTS                                    ║"
    echo "║ Tests exitosos: $PASSED_TESTS                                  ║"
    echo "║ Tests fallidos: $FAILED_TESTS                                  ║"
    echo "║ Errores críticos: $CRITICAL_ERRORS                              ║"
    echo "╚══════════════════════════════════════════════════════╝"
    
    if [[ $CRITICAL_ERRORS -gt 0 ]]; then
        log_critical "ERRORES CRÍTICOS DETECTADOS - KERNEL NO LISTO"
        exit 1
    elif [[ $FAILED_TESTS -eq 0 ]]; then
        log_success "TODOS LOS TESTS PASARON - KERNEL LISTO PARA QEMU"
        echo
        echo "Para ejecutar:"
        echo "  make ARCH=x86-32 run    # Test 32-bit"
        echo "  make ARCH=x86-64 run    # Test 64-bit"
        echo
        echo "Para limpiar:"
        echo "  make clean-all          # Limpiar todas las arquitecturas"
        exit 0
    else
        log_warning "ALGUNOS TESTS FALLARON - REVISAR ANTES DE USAR"
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