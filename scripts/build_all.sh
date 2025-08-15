#!/bin/bash

# IR0 Kernel - Script de compilación para todas las arquitecturas
# Autor: IR0 Kernel Team
# Versión: 1.0

set -e  # Salir en caso de error

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Función para mostrar mensajes
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

# Función para mostrar ayuda
show_help() {
    echo "IR0 Kernel - Script de compilación multi-arquitectura"
    echo ""
    echo "Uso: $0 [OPCIONES]"
    echo ""
    echo "Opciones:"
    echo "  -a, --arch-only      Compilar solo todas las arquitecturas (build target por defecto)"
    echo "  -t, --target-only    Compilar solo todos los build targets (arquitectura por defecto)"
    echo "  -c, --combinations   Compilar todas las combinaciones (recomendado)"
    echo "  -r, --run            Ejecutar en QEMU después de compilar"
    echo "  -d, --debug          Modo debug (más información)"
    echo "  -h, --help           Mostrar esta ayuda"
    echo ""
    echo "Ejemplos:"
    echo "  $0 -c              # Compilar todas las combinaciones"
    echo "  $0 -a -r           # Compilar todas las arquitecturas y ejecutar"
    echo "  $0 -t -d           # Compilar todos los targets en modo debug"
    echo ""
}

# Variables por defecto
BUILD_MODE="combinations"
RUN_QEMU=false
DEBUG_MODE=false

# Procesar argumentos
while [[ $# -gt 0 ]]; do
    case $1 in
        -a|--arch-only)
            BUILD_MODE="arch"
            shift
            ;;
        -t|--target-only)
            BUILD_MODE="target"
            shift
            ;;
        -c|--combinations)
            BUILD_MODE="combinations"
            shift
            ;;
        -r|--run)
            RUN_QEMU=true
            shift
            ;;
        -d|--debug)
            DEBUG_MODE=true
            shift
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        *)
            print_error "Opción desconocida: $1"
            show_help
            exit 1
            ;;
    esac
done

# Función para verificar dependencias
check_dependencies() {
    print_info "Verificando dependencias..."
    
    local missing_deps=()
    
    # Verificar make
    if ! command -v make &> /dev/null; then
        missing_deps+=("make")
    fi
    
    # Verificar gcc
    if ! command -v gcc &> /dev/null; then
        missing_deps+=("gcc")
    fi
    
    # Verificar nasm
    if ! command -v nasm &> /dev/null; then
        missing_deps+=("nasm")
    fi
    
    # Verificar grub-mkrescue
    if ! command -v grub-mkrescue &> /dev/null; then
        missing_deps+=("grub-mkrescue")
    fi
    
    # Verificar qemu (si se va a ejecutar)
    if [ "$RUN_QEMU" = true ]; then
        if ! command -v qemu-system-x86_64 &> /dev/null; then
            missing_deps+=("qemu-system-x86_64")
        fi
        if ! command -v qemu-system-i386 &> /dev/null; then
            missing_deps+=("qemu-system-i386")
        fi
        if ! command -v qemu-system-aarch64 &> /dev/null; then
            missing_deps+=("qemu-system-aarch64")
        fi
        if ! command -v qemu-system-arm &> /dev/null; then
            missing_deps+=("qemu-system-arm")
        fi
    fi
    
    if [ ${#missing_deps[@]} -ne 0 ]; then
        print_error "Dependencias faltantes: ${missing_deps[*]}"
        print_info "Instale las dependencias con:"
        echo "  sudo apt-get install build-essential nasm grub-pc-bin xorriso qemu-system-x86"
        echo "  sudo apt-get install gcc-aarch64-linux-gnu gcc-arm-linux-gnueabi"
        echo "  sudo apt-get install qemu-system-arm"
        exit 1
    fi
    
    print_success "Todas las dependencias están instaladas"
}

# Función para limpiar builds anteriores
clean_previous_builds() {
    print_info "Limpiando builds anteriores..."
    make clean-all > /dev/null 2>&1 || true
    print_success "Limpieza completada"
}

# Función para compilar una combinación específica
compile_combination() {
    local arch=$1
    local target=$2
    
    print_info "Compilando $arch-$target..."
    
    if [ "$DEBUG_MODE" = true ]; then
        make ARCH=$arch BUILD_TARGET=$target
    else
        make ARCH=$arch BUILD_TARGET=$target > /dev/null 2>&1
    fi
    
    if [ $? -eq 0 ]; then
        print_success "$arch-$target compilado correctamente"
        return 0
    else
        print_error "Error compilando $arch-$target"
        return 1
    fi
}

# Función para ejecutar en QEMU
run_qemu() {
    local arch=$1
    local target=$2
    
    print_info "Ejecutando $arch-$target en QEMU..."
    
    if [ "$arch" = "x86-64" ]; then
        qemu-system-x86_64 -cdrom kernel-$arch-$target.iso -m 512M -display gtk &
    elif [ "$arch" = "x86-32" ]; then
        qemu-system-i386 -cdrom kernel-$arch-$target.iso -m 512M -display gtk &
    elif [ "$arch" = "arm64" ]; then
        qemu-system-aarch64 -M virt -cpu cortex-a57 -m 512M -kernel kernel-$arch-$target.bin -display gtk &
    elif [ "$arch" = "arm32" ]; then
        qemu-system-arm -M vexpress-a9 -cpu cortex-a9 -m 512M -kernel kernel-$arch-$target.bin -display gtk &
    fi
    
    print_success "QEMU iniciado para $arch-$target"
}

# Función principal de compilación
main_build() {
    print_info "Iniciando compilación en modo: $BUILD_MODE"
    
    local success_count=0
    local total_count=0
    
    case $BUILD_MODE in
        "arch")
            # Compilar todas las arquitecturas con build target por defecto
            for arch in "x86-32" "x86-64" "arm32" "arm64"; do
                total_count=$((total_count + 1))
                if compile_combination $arch "desktop"; then
                    success_count=$((success_count + 1))
                    if [ "$RUN_QEMU" = true ]; then
                        run_qemu $arch "desktop"
                    fi
                fi
            done
            ;;
        "target")
            # Compilar todos los build targets con arquitectura por defecto
            for target in "desktop" "server" "iot" "embedded"; do
                total_count=$((total_count + 1))
                if compile_combination "x86-32" $target; then
                    success_count=$((success_count + 1))
                    if [ "$RUN_QEMU" = true ]; then
                        run_qemu "x86-32" $target
                    fi
                fi
            done
            ;;
        "combinations")
            # Compilar todas las combinaciones
            for arch in "x86-32" "x86-64" "arm32" "arm64"; do
                for target in "desktop" "server" "iot" "embedded"; do
                    total_count=$((total_count + 1))
                    if compile_combination $arch $target; then
                        success_count=$((success_count + 1))
                        if [ "$RUN_QEMU" = true ]; then
                            run_qemu $arch $target
                        fi
                    fi
                done
            done
            ;;
    esac
    
    # Mostrar resumen
    echo ""
    print_info "Resumen de compilación:"
    echo "  Exitosas: $success_count/$total_count"
    echo "  Fallidas: $((total_count - success_count))/$total_count"
    
    if [ $success_count -eq $total_count ]; then
        print_success "¡Todas las compilaciones fueron exitosas!"
        return 0
    else
        print_warning "Algunas compilaciones fallaron"
        return 1
    fi
}

# Función para mostrar archivos generados
show_generated_files() {
    print_info "Archivos generados:"
    ls -la kernel-*.bin kernel-*.iso 2>/dev/null || print_warning "No se encontraron archivos generados"
}

# Script principal
main() {
    echo "============================================================"
    echo "IR0 Kernel - Sistema de Compilación Multi-Arquitectura"
    echo "============================================================"
    echo ""
    
    # Verificar que estamos en el directorio correcto
    if [ ! -f "Makefile" ]; then
        print_error "No se encontró el Makefile. Ejecute este script desde el directorio raíz del kernel."
        exit 1
    fi
    
    # Verificar dependencias
    check_dependencies
    
    # Limpiar builds anteriores
    clean_previous_builds
    
    # Ejecutar compilación
    if main_build; then
        print_success "Compilación completada exitosamente"
        show_generated_files
    else
        print_error "La compilación falló"
        exit 1
    fi
}

# Ejecutar script principal
main "$@"
