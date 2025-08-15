#!/bin/bash

# Script para verificar que todos los archivos necesarios existen antes de compilar
# Uso: ./scripts/verify_build.sh [ARCH] [BUILD_TARGET]

set -e

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Función para imprimir mensajes
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Función para verificar si un archivo existe
check_file() {
    if [ -f "$1" ]; then
        print_success "✓ $1"
        return 0
    else
        print_error "✗ $1 (FALTANTE)"
        return 1
    fi
}

# Función para verificar si un directorio existe
check_dir() {
    if [ -d "$1" ]; then
        print_success "✓ $1/"
        return 0
    else
        print_error "✗ $1/ (FALTANTE)"
        return 1
    fi
}

# Obtener arquitectura y build target de los argumentos o usar valores por defecto
ARCH=${1:-x86-32}
BUILD_TARGET=${2:-desktop}

print_info "Verificando build para ARCH=$ARCH BUILD_TARGET=$BUILD_TARGET"
echo ""

# Verificar archivos comunes
print_info "Verificando archivos comunes..."
echo ""

# Archivos de includes
check_file "includes/ir0/kernel.h"
check_file "includes/ir0/print.c"
check_file "includes/ir0/print.h"
check_file "includes/string.c"
check_file "includes/string.h"
check_file "includes/stddef.h"
check_file "includes/stdint.h"

# Archivos del kernel
check_file "kernel/kernel_start.c"
check_file "kernel/kernel_start.h"

# Archivos de interrupciones
check_file "interrupt/idt.c"
check_file "interrupt/isr_handlers.c"
check_file "interrupt/isr_handlers.h"

# Archivos de memoria
check_file "memory/heap_allocator.c"
check_file "memory/heap_allocator.h"
check_file "memory/physical_allocator.c"
check_file "memory/physical_allocator.h"
check_file "memory/ondemand-paging.c"
check_file "memory/ondemand-paging.h"
check_file "memory/vallocator.c"

# Archivos de timer
check_file "drivers/timer/pit/pit.c"
check_file "drivers/timer/pit/pit.h"
check_file "drivers/timer/clock_system.c"
check_file "drivers/timer/clock_system.h"
check_file "drivers/timer/best_clock.c"
check_file "drivers/timer/hpet/hpet.c"
check_file "drivers/timer/hpet/hpet.h"
check_file "drivers/timer/hpet/find_hpet.c"
check_file "drivers/timer/hpet/find_hpet.h"
check_file "drivers/timer/lapic/lapic.c"
check_file "drivers/timer/lapic/lapic.h"

# Archivos del scheduler
check_file "kernel/scheduler/priority_scheduler.c"
check_file "kernel/scheduler/round-robin_scheduler.c"
check_file "kernel/scheduler/sched_central.c"
check_file "kernel/scheduler/cfs_scheduler.c"
check_file "kernel/scheduler/scheduler_detection.c"
check_file "kernel/scheduler/scheduler_types.h"
check_file "kernel/scheduler/scheduler.h"
check_file "kernel/scheduler/task_impl.c"
check_file "kernel/scheduler/task.h"

# Archivos de setup
check_file "setup/kernel_config.c"
check_file "setup/kernel_config.h"

# Archivos de panic
check_file "includes/ir0/panic/panic.c"
check_file "includes/ir0/panic/panic.h"

# Archivos de arch/common
check_file "arch/common/arch_interface.c"
check_file "arch/common/arch_interface.h"

echo ""

# Verificar archivos específicos de arquitectura
print_info "Verificando archivos específicos de $ARCH..."
echo ""

if [ "$ARCH" = "x86-64" ]; then
    check_file "arch/x86-64/sources/arch_x64.c"
    check_file "arch/x86-64/sources/arch_x64.h"
    check_file "arch/x86-64/sources/fault.c"
    check_file "arch/x86-64/sources/idt_arch_x64.c"
    check_file "arch/x86-64/sources/idt_arch_x64.h"
    check_file "arch/x86-64/asm/boot_x64.asm"
    check_file "arch/x86-64/linker.ld"
    check_file "arch/x86-64/grub.cfg"
    check_file "memory/arch/x86-64/Paging_x64.c"
    check_file "memory/arch/x86-64/Paging_x64.h"
    check_file "memory/arch/x86-64/mmu_x64.c"
    check_file "kernel/scheduler/switch/switch_x64.asm"
    check_file "interrupt/arch/x86-64/interrupt.asm"
    
elif [ "$ARCH" = "x86-32" ]; then
    check_file "arch/x86-32/sources/arch_x86.c"
    check_file "arch/x86-32/sources/arch_x86.h"
    check_file "arch/x86-32/sources/idt_arch_x86.c"
    check_file "arch/x86-32/sources/idt_arch_x86.h"
    check_file "arch/x86-32/asm/boot_x86.asm"
    check_file "arch/x86-32/linker.ld"
    check_file "arch/x86-32/grub.cfg"
    check_file "memory/arch/x_86-32/Paging_x86-32.c"
    check_file "memory/arch/x_86-32/Paging_x86-32.h"
    check_file "memory/arch/x_86-32/mmu_x86-32.c"
    check_file "kernel/scheduler/switch/switch_x86.asm"
    check_file "interrupt/arch/x86-32/interrupt.asm"
    
elif [ "$ARCH" = "arm64" ]; then
    print_warning "Verificación de ARM64 no implementada completamente"
    check_dir "arch/arm64"
    
elif [ "$ARCH" = "arm32" ]; then
    print_warning "Verificación de ARM32 no implementada completamente"
    check_dir "arch/arm32"
fi

echo ""

# Verificar archivos condicionales según build target
print_info "Verificando archivos específicos de $BUILD_TARGET..."
echo ""

if [ "$BUILD_TARGET" = "desktop" ] || [ "$BUILD_TARGET" = "server" ] || [ "$BUILD_TARGET" = "iot" ]; then
    check_file "fs/vfs_simple.c"
    check_file "fs/vfs_simple.h"
    check_file "fs/vfs.c"
    check_file "fs/vfs.h"
fi

echo ""

# Verificar Makefiles
print_info "Verificando Makefiles..."
echo ""

check_file "Makefile"
check_file "arch/x86-64/Makefile"
check_file "arch/x86-32/Makefile"
check_file "kernel/Makefile"
check_file "interrupt/Makefile"
check_file "memory/Makefile"
check_file "drivers/timer/Makefile"
check_file "kernel/scheduler/Makefile"
check_file "includes/ir0/Makefile"
check_file "setup/Makefile"
check_file "memory/arch/x86-64/Makefile"
check_file "memory/arch/x_86-32/Makefile"
check_file "arch/common/Makefile"
check_file "includes/ir0/panic/Makefile"

if [ "$BUILD_TARGET" = "desktop" ] || [ "$BUILD_TARGET" = "server" ] || [ "$BUILD_TARGET" = "iot" ]; then
    check_file "fs/Makefile"
fi

echo ""

# Verificar herramientas necesarias
print_info "Verificando herramientas de compilación..."
echo ""

# Verificar compilador
if command -v gcc >/dev/null 2>&1; then
    print_success "✓ gcc disponible"
else
    print_error "✗ gcc no encontrado"
fi

# Verificar nasm
if command -v nasm >/dev/null 2>&1; then
    print_success "✓ nasm disponible"
else
    print_error "✗ nasm no encontrado"
fi

# Verificar ld
if command -v ld >/dev/null 2>&1; then
    print_success "✓ ld disponible"
else
    print_error "✗ ld no encontrado"
fi

# Verificar grub-mkrescue
if command -v grub-mkrescue >/dev/null 2>&1; then
    print_success "✓ grub-mkrescue disponible"
else
    print_warning "⚠ grub-mkrescue no encontrado (necesario para crear ISOs)"
fi

# Verificar xorriso
if command -v xorriso >/dev/null 2>&1; then
    print_success "✓ xorriso disponible"
else
    print_warning "⚠ xorriso no encontrado (necesario para crear ISOs)"
fi

echo ""

print_info "Verificación completada para $ARCH-$BUILD_TARGET"
print_info "Si todos los archivos están presentes, puedes ejecutar:"
print_info "  make ARCH=$ARCH BUILD_TARGET=$BUILD_TARGET"
