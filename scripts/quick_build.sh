#!/bin/bash

# Script para compilar y bootear fácilmente el kernel IR0
# Uso: ./scripts/quick_build.sh [ARCH] [BUILD_TARGET] [ACTION]

set -e

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
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

print_header() {
    echo -e "${PURPLE}============================================================${NC}"
    echo -e "${PURPLE} $1${NC}"
    echo -e "${PURPLE}============================================================${NC}"
}

# Función para mostrar ayuda
show_help() {
    echo "IR0 Kernel Quick Build Script"
    echo ""
    echo "Uso: $0 [ARCH] [BUILD_TARGET] [ACTION]"
    echo ""
    echo "Arquitecturas soportadas:"
    echo "  x86-32  - 32-bit (i386)"
    echo "  x86-64  - 64-bit (x86_64)"
    echo "  arm32   - ARM 32-bit (ARMv7)"
    echo "  arm64   - ARM 64-bit (ARMv8)"
    echo ""
    echo "Build Targets soportados:"
    echo "  desktop   - Sistema de escritorio"
    echo "  server    - Sistema servidor"
    echo "  iot       - Sistema IoT"
    echo "  embedded  - Sistema embebido"
    echo ""
    echo "Acciones disponibles:"
    echo "  build     - Solo compilar (por defecto)"
    echo "  run       - Compilar y ejecutar en QEMU"
    echo "  debug     - Compilar y ejecutar con debug"
    echo "  clean     - Limpiar archivos de compilación"
    echo "  verify    - Verificar archivos necesarios"
    echo "  all       - Compilar para todas las arquitecturas"
    echo ""
    echo "Ejemplos:"
    echo "  $0 x86-64 desktop run     - Compilar y ejecutar x86-64 desktop"
    echo "  $0 x86-32 server build    - Solo compilar x86-32 server"
    echo "  $0 x86-64 desktop debug   - Compilar y debug x86-64 desktop"
    echo "  $0 verify                 - Verificar archivos necesarios"
    echo "  $0 all                    - Compilar todas las arquitecturas"
}

# Obtener argumentos
ARCH=${1:-x86-32}
BUILD_TARGET=${2:-desktop}
ACTION=${3:-build}

# Validar arquitectura
VALID_ARCHS="x86-32 x86-64 arm32 arm64"
if [[ ! " $VALID_ARCHS " =~ " $ARCH " ]]; then
    if [ "$ARCH" = "help" ] || [ "$ARCH" = "-h" ] || [ "$ARCH" = "--help" ]; then
        show_help
        exit 0
    fi
    print_error "Arquitectura no válida: $ARCH"
    print_info "Arquitecturas válidas: $VALID_ARCHS"
    exit 1
fi

# Validar build target
VALID_TARGETS="desktop server iot embedded"
if [[ ! " $VALID_TARGETS " =~ " $BUILD_TARGET " ]]; then
    print_error "Build target no válido: $BUILD_TARGET"
    print_info "Build targets válidos: $VALID_TARGETS"
    exit 1
fi

# Validar acción
VALID_ACTIONS="build run debug clean verify all"
if [[ ! " $VALID_ACTIONS " =~ " $ACTION " ]]; then
    print_error "Acción no válida: $ACTION"
    print_info "Acciones válidas: $VALID_ACTIONS"
    exit 1
fi

# Función para verificar dependencias
check_dependencies() {
    print_info "Verificando dependencias..."
    
    local missing_deps=()
    
    # Verificar compilador
    if ! command -v gcc >/dev/null 2>&1; then
        missing_deps+=("gcc")
    fi
    
    # Verificar nasm
    if ! command -v nasm >/dev/null 2>&1; then
        missing_deps+=("nasm")
    fi
    
    # Verificar ld
    if ! command -v ld >/dev/null 2>&1; then
        missing_deps+=("binutils")
    fi
    
    # Verificar QEMU si se va a ejecutar
    if [ "$ACTION" = "run" ] || [ "$ACTION" = "debug" ]; then
        case $ARCH in
            x86-64)
                if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
                    missing_deps+=("qemu-system-x86_64")
                fi
                ;;
            x86-32)
                if ! command -v qemu-system-i386 >/dev/null 2>&1; then
                    missing_deps+=("qemu-system-i386")
                fi
                ;;
            arm64)
                if ! command -v qemu-system-aarch64 >/dev/null 2>&1; then
                    missing_deps+=("qemu-system-aarch64")
                fi
                ;;
            arm32)
                if ! command -v qemu-system-arm >/dev/null 2>&1; then
                    missing_deps+=("qemu-system-arm")
                fi
                ;;
        esac
    fi
    
    # Verificar grub-mkrescue si se va a crear ISO
    if ! command -v grub-mkrescue >/dev/null 2>&1; then
        print_warning "grub-mkrescue no encontrado (necesario para crear ISOs)"
    fi
    
    if [ ${#missing_deps[@]} -ne 0 ]; then
        print_error "Dependencias faltantes: ${missing_deps[*]}"
        print_info "Instala las dependencias con:"
        print_info "  sudo apt-get install ${missing_deps[*]}"
        exit 1
    fi
    
    print_success "Todas las dependencias están disponibles"
}

# Función para limpiar
clean_build() {
    print_info "Limpiando archivos de compilación..."
    make clean-all
    print_success "Limpieza completada"
}

# Función para verificar archivos
verify_files() {
    print_info "Ejecutando verificación de archivos..."
    ./scripts/verify_build.sh "$ARCH" "$BUILD_TARGET"
}

# Función para compilar
build_kernel() {
    print_header "Compilando IR0 Kernel para $ARCH-$BUILD_TARGET"
    
    print_info "Arquitectura: $ARCH"
    print_info "Build Target: $BUILD_TARGET"
    print_info "Acción: $ACTION"
    echo ""
    
    # Verificar dependencias
    check_dependencies
    
    # Verificar archivos si es la primera vez
    if [ ! -f "kernel-$ARCH-$BUILD_TARGET.bin" ]; then
        print_info "Primera compilación, verificando archivos..."
        verify_files
        echo ""
    fi
    
    # Compilar
    print_info "Iniciando compilación..."
    if make ARCH="$ARCH" BUILD_TARGET="$BUILD_TARGET"; then
        print_success "Compilación exitosa!"
        print_info "Kernel generado: kernel-$ARCH-$BUILD_TARGET.bin"
        if [ -f "kernel-$ARCH-$BUILD_TARGET.iso" ]; then
            print_info "ISO generado: kernel-$ARCH-$BUILD_TARGET.iso"
        fi
    else
        print_error "Error en la compilación"
        exit 1
    fi
}

# Función para ejecutar en QEMU
run_qemu() {
    print_header "Ejecutando IR0 Kernel en QEMU"
    
    # Verificar que el kernel existe
    if [ ! -f "kernel-$ARCH-$BUILD_TARGET.bin" ]; then
        print_error "Kernel no encontrado. Compilando primero..."
        build_kernel
    fi
    
    print_info "Iniciando QEMU para $ARCH-$BUILD_TARGET..."
    
    case $ARCH in
        x86-64)
            if [ -f "kernel-$ARCH-$BUILD_TARGET.iso" ]; then
                qemu-system-x86_64 -cdrom "kernel-$ARCH-$BUILD_TARGET.iso" -m 512M -display gtk
            else
                print_error "ISO no encontrado para x86-64"
                exit 1
            fi
            ;;
        x86-32)
            if [ -f "kernel-$ARCH-$BUILD_TARGET.iso" ]; then
                qemu-system-i386 -cdrom "kernel-$ARCH-$BUILD_TARGET.iso" -m 512M -display gtk
            else
                print_error "ISO no encontrado para x86-32"
                exit 1
            fi
            ;;
        arm64)
            qemu-system-aarch64 -M virt -cpu cortex-a57 -m 512M -kernel "kernel-$ARCH-$BUILD_TARGET.bin" -display gtk
            ;;
        arm32)
            qemu-system-arm -M vexpress-a9 -cpu cortex-a9 -m 512M -kernel "kernel-$ARCH-$BUILD_TARGET.bin" -display gtk
            ;;
    esac
}

# Función para debug en QEMU
debug_qemu() {
    print_header "Ejecutando IR0 Kernel en QEMU con Debug"
    
    # Verificar que el kernel existe
    if [ ! -f "kernel-$ARCH-$BUILD_TARGET.bin" ]; then
        print_error "Kernel no encontrado. Compilando primero..."
        build_kernel
    fi
    
    print_info "Iniciando QEMU con debug para $ARCH-$BUILD_TARGET..."
    
    case $ARCH in
        x86-64)
            if [ -f "kernel-$ARCH-$BUILD_TARGET.iso" ]; then
                qemu-system-x86_64 -cdrom "kernel-$ARCH-$BUILD_TARGET.iso" -m 512M -display gtk -d int,cpu_reset -D qemu_debug_$ARCH.log
            else
                print_error "ISO no encontrado para x86-64"
                exit 1
            fi
            ;;
        x86-32)
            if [ -f "kernel-$ARCH-$BUILD_TARGET.iso" ]; then
                qemu-system-i386 -cdrom "kernel-$ARCH-$BUILD_TARGET.iso" -m 512M -display gtk -d int,cpu_reset -D qemu_debug_$ARCH.log
            else
                print_error "ISO no encontrado para x86-32"
                exit 1
            fi
            ;;
        *)
            print_warning "Debug no implementado para $ARCH"
            run_qemu
            ;;
    esac
}

# Función para compilar todas las arquitecturas
build_all() {
    print_header "Compilando IR0 Kernel para todas las arquitecturas"
    
    local archs=("x86-32" "x86-64")
    local targets=("desktop" "server" "iot" "embedded")
    
    for arch in "${archs[@]}"; do
        for target in "${targets[@]}"; do
            print_info "Compilando $arch-$target..."
            if make ARCH="$arch" BUILD_TARGET="$target" >/dev/null 2>&1; then
                print_success "✓ $arch-$target compilado"
            else
                print_error "✗ Error compilando $arch-$target"
            fi
        done
    done
    
    print_success "Compilación de todas las arquitecturas completada"
}

# Ejecutar acción correspondiente
case $ACTION in
    build)
        build_kernel
        ;;
    run)
        build_kernel
        echo ""
        run_qemu
        ;;
    debug)
        build_kernel
        echo ""
        debug_qemu
        ;;
    clean)
        clean_build
        ;;
    verify)
        verify_files
        ;;
    all)
        build_all
        ;;
esac

print_success "Operación completada exitosamente!"
