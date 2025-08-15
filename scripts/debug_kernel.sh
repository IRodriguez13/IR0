#!/bin/bash

# debug_kernel.sh - Script para debugging del kernel con captura de logs
# Autor: IR0 Kernel Team
# Fecha: 2025-08-15

set -e

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

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

# Verificar argumentos
if [ $# -eq 0 ]; then
    print_error "Uso: $0 <arch> [target]"
    print_info "Ejemplos:"
    print_info "  $0 x86-64 desktop"
    print_info "  $0 x86-32 desktop"
    exit 1
fi

ARCH=$1
TARGET=${2:-desktop}

print_info "=== IR0 Kernel Debug Mode ==="
print_info "Arquitectura: $ARCH"
print_info "Target: $TARGET"

# Verificar que estamos en el directorio correcto
if [ ! -f "Makefile" ]; then
    print_error "No se encontró Makefile. Ejecuta este script desde el directorio raíz del kernel."
    exit 1
fi

# Crear directorio de logs
LOG_DIR="debug_logs"
mkdir -p "$LOG_DIR"

# Timestamp para el log
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
LOG_FILE="$LOG_DIR/kernel_debug_${ARCH}_${TARGET}_${TIMESTAMP}.log"

print_info "Log file: $LOG_FILE"

# Compilar el kernel
print_info "Compilando kernel $ARCH-$TARGET..."
if make ARCH=$ARCH BUILD_TARGET=$TARGET > "$LOG_FILE" 2>&1; then
    print_success "Kernel compilado correctamente"
else
    print_error "Error compilando kernel. Revisa $LOG_FILE"
    exit 1
fi

# Verificar que el archivo existe
KERNEL_FILE="kernel-${ARCH}-${TARGET}.bin"
if [ ! -f "$KERNEL_FILE" ]; then
    print_error "No se encontró $KERNEL_FILE"
    exit 1
fi

print_success "Kernel file: $KERNEL_FILE ($(stat -c%s "$KERNEL_FILE") bytes)"

# Información del archivo
print_info "Información del kernel:"
file "$KERNEL_FILE" | tee -a "$LOG_FILE"

# Verificar símbolos importantes
print_info "Verificando símbolos importantes..."
if nm "$KERNEL_FILE" 2>/dev/null | grep -q "kmain_x64\|kmain_x32"; then
    print_success "Símbolo de entrada encontrado"
    nm "$KERNEL_FILE" 2>/dev/null | grep "kmain_x" | tee -a "$LOG_FILE"
else
    print_warning "Símbolo de entrada no encontrado"
fi

# Ejecutar en QEMU con captura de logs
print_info "Ejecutando kernel en QEMU con captura de logs..."

# Determinar comando QEMU según arquitectura
if [ "$ARCH" = "x86-64" ]; then
    QEMU_CMD="qemu-system-x86_64 -cdrom kernel-${ARCH}-${TARGET}.iso -m 512M -display gtk -serial file:${LOG_FILE}.qemu"
elif [ "$ARCH" = "x86-32" ]; then
    QEMU_CMD="qemu-system-i386 -cdrom kernel-${ARCH}-${TARGET}.iso -m 512M -display gtk -serial file:${LOG_FILE}.qemu"
else
    print_error "Arquitectura no soportada: $ARCH"
    exit 1
fi

print_info "Comando QEMU: $QEMU_CMD"
print_info "Presiona Ctrl+C para detener QEMU"

# Ejecutar QEMU
echo "=== QEMU Output ===" >> "$LOG_FILE"
echo "Comando: $QEMU_CMD" >> "$LOG_FILE"
echo "Timestamp: $(date)" >> "$LOG_FILE"
echo "" >> "$LOG_FILE"

# Ejecutar QEMU en background y capturar output
eval "$QEMU_CMD" >> "$LOG_FILE" 2>&1 &
QEMU_PID=$!

print_info "QEMU ejecutándose con PID: $QEMU_PID"
print_info "Logs se están guardando en: $LOG_FILE"

# Esperar un poco y mostrar logs
sleep 5
if [ -f "${LOG_FILE}.qemu" ]; then
    print_info "Últimas líneas del log QEMU:"
    tail -20 "${LOG_FILE}.qemu" 2>/dev/null || print_warning "No hay output QEMU aún"
fi

print_info ""
print_info "Para ver logs en tiempo real:"
print_info "  tail -f $LOG_FILE"
print_info "  tail -f ${LOG_FILE}.qemu"
print_info ""
print_info "Para detener QEMU:"
print_info "  kill $QEMU_PID"

# Esperar a que el usuario presione Ctrl+C
trap "print_info 'Deteniendo QEMU...'; kill $QEMU_PID 2>/dev/null; print_success 'Debug completado. Revisa $LOG_FILE'; exit 0" INT

# Mantener el script corriendo
while kill -0 $QEMU_PID 2>/dev/null; do
    sleep 1
done

print_success "QEMU terminó. Revisa $LOG_FILE para más detalles."
