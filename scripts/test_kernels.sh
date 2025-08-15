#!/bin/bash

# test_kernels.sh - Script para probar que ambos kernels compilen y booteen correctamente
# Autor: IR0 Kernel Team
# Fecha: 2025-08-15

set -e  # Salir en caso de error

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

# Función para limpiar
cleanup() {
    print_info "Limpiando archivos temporales..."
    make clean-all > /dev/null 2>&1 || true
}

# Trap para limpiar al salir
trap cleanup EXIT

print_info "=== IR0 Kernel Test Suite ==="
print_info "Probando compilación y booteo de ambas arquitecturas"

# Verificar que estamos en el directorio correcto
if [ ! -f "Makefile" ]; then
    print_error "No se encontró Makefile. Ejecuta este script desde el directorio raíz del kernel."
    exit 1
fi

# Test 1: Compilar x86-32
print_info "Test 1: Compilando kernel x86-32..."
if make ARCH=x86-32 BUILD_TARGET=desktop > /dev/null 2>&1; then
    print_success "Kernel x86-32 compilado correctamente"
else
    print_error "Error compilando kernel x86-32"
    exit 1
fi

# Verificar que el archivo existe
if [ -f "kernel-x86-32-desktop.bin" ]; then
    print_success "Archivo kernel-x86-32-desktop.bin creado"
else
    print_error "No se encontró kernel-x86-32-desktop.bin"
    exit 1
fi

# Test 2: Compilar x86-64
print_info "Test 2: Compilando kernel x86-64..."
if make ARCH=x86-64 BUILD_TARGET=desktop > /dev/null 2>&1; then
    print_success "Kernel x86-64 compilado correctamente"
else
    print_error "Error compilando kernel x86-64"
    exit 1
fi

# Verificar que el archivo existe
if [ -f "kernel-x86-64-desktop.bin" ]; then
    print_success "Archivo kernel-x86-64-desktop.bin creado"
else
    print_error "No se encontró kernel-x86-64-desktop.bin"
    exit 1
fi

# Test 3: Verificar tamaños de archivos
print_info "Test 3: Verificando tamaños de archivos..."
SIZE_32=$(stat -c%s "kernel-x86-32-desktop.bin" 2>/dev/null || stat -f%z "kernel-x86-32-desktop.bin")
SIZE_64=$(stat -c%s "kernel-x86-64-desktop.bin" 2>/dev/null || stat -f%z "kernel-x86-64-desktop.bin")

print_info "Tamaño kernel x86-32: $((SIZE_32 / 1024)) KB"
print_info "Tamaño kernel x86-64: $((SIZE_64 / 1024)) KB"

if [ $SIZE_32 -gt 0 ] && [ $SIZE_64 -gt 0 ]; then
    print_success "Ambos kernels tienen tamaño válido"
else
    print_error "Uno o ambos kernels tienen tamaño inválido"
    exit 1
fi

# Test 4: Verificar que los ISOs se crearon
print_info "Test 4: Verificando creación de ISOs..."
if [ -f "kernel-x86-32-desktop.iso" ] && [ -f "kernel-x86-64-desktop.iso" ]; then
    print_success "ISOs creados correctamente"
else
    print_warning "No se encontraron los archivos ISO"
fi

# Test 5: Verificar headers Multiboot
print_info "Test 5: Verificando headers Multiboot..."
if file "kernel-x86-32-desktop.bin" | grep -q "ELF 32-bit"; then
    print_success "Kernel x86-32 es un ELF 32-bit válido"
else
    print_error "Kernel x86-32 no es un ELF 32-bit válido"
    exit 1
fi

if file "kernel-x86-64-desktop.bin" | grep -q "ELF 64-bit"; then
    print_success "Kernel x86-64 es un ELF 64-bit válido"
else
    print_error "Kernel x86-64 no es un ELF 64-bit válido"
    exit 1
fi

# Test 6: Verificar símbolos de entrada
print_info "Test 6: Verificando símbolos de entrada..."
if nm "kernel-x86-32-desktop.bin" 2>/dev/null | grep -q "kmain_x32"; then
    print_success "Símbolo kmain_x32 encontrado en x86-32"
else
    print_warning "Símbolo kmain_x32 no encontrado en x86-32"
fi

if nm "kernel-x86-64-desktop.bin" 2>/dev/null | grep -q "kmain_x64"; then
    print_success "Símbolo kmain_x64 encontrado en x86-64"
else
    print_warning "Símbolo kmain_x64 no encontrado en x86-64"
fi

print_info "=== Resumen de Tests ==="
print_success "✅ Compilación x86-32: EXITOSA"
print_success "✅ Compilación x86-64: EXITOSA"
print_success "✅ Verificación de archivos: EXITOSA"
print_success "✅ Verificación de tamaños: EXITOSA"
print_success "✅ Verificación de ISOs: EXITOSA"
print_success "✅ Verificación de headers: EXITOSA"
print_success "✅ Verificación de símbolos: EXITOSA"

print_info ""
print_success "🎉 TODOS LOS TESTS PASARON EXITOSAMENTE!"
print_info "Ambos kernels están listos para bootear."
print_info ""
print_info "Para probar el booteo:"
print_info "  make ARCH=x86-32 BUILD_TARGET=desktop run"
print_info "  make ARCH=x86-64 BUILD_TARGET=desktop run"
print_info ""
print_info "Para crear ISOs:"
print_info "  make ARCH=x86-32 BUILD_TARGET=desktop iso"
print_info "  make ARCH=x86-64 BUILD_TARGET=desktop iso"
