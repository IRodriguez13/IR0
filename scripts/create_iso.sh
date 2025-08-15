#!/bin/bash

# IR0 Kernel - Script para crear ISOs personalizadas
# Autor: IR0 Kernel Team
# Versión: 1.0

set -e

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

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

show_help() {
    echo "IR0 Kernel - Creador de ISOs personalizadas"
    echo ""
    echo "Uso: $0 [OPCIONES]"
    echo ""
    echo "Opciones:"
    echo "  -a, --arch ARCH       Arquitectura (x86-32, x86-64)"
    echo "  -t, --target TARGET   Build target (desktop, server, iot, embedded)"
    echo "  -o, --output NAME     Nombre del archivo ISO de salida"
    echo "  -c, --config FILE     Archivo de configuración personalizada"
    echo "  -b, --boot-args ARGS  Argumentos de boot adicionales"
    echo "  -d, --debug           Modo debug"
    echo "  -h, --help            Mostrar esta ayuda"
    echo ""
    echo "Ejemplos:"
    echo "  $0 -a x86-64 -t desktop -o ir0-desktop.iso"
    echo "  $0 -a x86-32 -t server -b 'console=ttyS0' -o ir0-server.iso"
    echo ""
}

# Variables por defecto
ARCH=""
TARGET=""
OUTPUT_NAME=""
CONFIG_FILE=""
BOOT_ARGS=""
DEBUG_MODE=false

# Procesar argumentos
while [[ $# -gt 0 ]]; do
    case $1 in
        -a|--arch)
            ARCH="$2"
            shift 2
            ;;
        -t|--target)
            TARGET="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT_NAME="$2"
            shift 2
            ;;
        -c|--config)
            CONFIG_FILE="$2"
            shift 2
            ;;
        -b|--boot-args)
            BOOT_ARGS="$2"
            shift 2
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

# Validar argumentos requeridos
if [ -z "$ARCH" ] || [ -z "$TARGET" ]; then
    print_error "Debe especificar arquitectura (-a) y build target (-t)"
    show_help
    exit 1
fi

# Validar arquitectura
if [ "$ARCH" != "x86-32" ] && [ "$ARCH" != "x86-64" ] && [ "$ARCH" != "arm32" ] && [ "$ARCH" != "arm64" ]; then
    print_error "Arquitectura no válida: $ARCH. Use x86-32, x86-64, arm32, o arm64"
    exit 1
fi

# Validar build target
if [ "$TARGET" != "desktop" ] && [ "$TARGET" != "server" ] && [ "$TARGET" != "iot" ] && [ "$TARGET" != "embedded" ]; then
    print_error "Build target no válido: $TARGET. Use desktop, server, iot, o embedded"
    exit 1
fi

# Generar nombre de salida si no se especifica
if [ -z "$OUTPUT_NAME" ]; then
    OUTPUT_NAME="ir0-kernel-${ARCH}-${TARGET}.iso"
fi

# Función para verificar dependencias
check_dependencies() {
    print_info "Verificando dependencias..."
    
    local missing_deps=()
    
    if ! command -v grub-mkrescue &> /dev/null; then
        missing_deps+=("grub-mkrescue")
    fi
    
    if ! command -v xorriso &> /dev/null; then
        missing_deps+=("xorriso")
    fi
    
    if [ ${#missing_deps[@]} -ne 0 ]; then
        print_error "Dependencias faltantes: ${missing_deps[*]}"
        print_info "Instale las dependencias con:"
        echo "  sudo apt-get install grub-pc-bin xorriso"
        exit 1
    fi
    
    print_success "Todas las dependencias están instaladas"
}

# Función para compilar el kernel
compile_kernel() {
    print_info "Compilando kernel para $ARCH-$TARGET..."
    
    if [ "$DEBUG_MODE" = true ]; then
        make ARCH=$ARCH BUILD_TARGET=$TARGET
    else
        make ARCH=$ARCH BUILD_TARGET=$TARGET > /dev/null 2>&1
    fi
    
    if [ $? -eq 0 ]; then
        print_success "Kernel compilado correctamente"
    else
        print_error "Error compilando el kernel"
        exit 1
    fi
}

# Función para crear configuración GRUB personalizada
create_custom_grub_config() {
    local grub_dir="iso-custom/boot/grub"
    local grub_config="$grub_dir/grub.cfg"
    
    print_info "Creando configuración GRUB personalizada..."
    
    # Crear directorio
    mkdir -p "$grub_dir"
    
    # Crear configuración base
    cat > "$grub_config" << EOF
set timeout=5
set default=0

menuentry "IR0 Kernel $ARCH-$TARGET" {
    set root=(cd0)
    linux /boot/kernel-$ARCH-$TARGET.bin $BOOT_ARGS
    boot
}

menuentry "IR0 Kernel $ARCH-$TARGET (Debug)" {
    set root=(cd0)
    linux /boot/kernel-$ARCH-$TARGET.bin debug $BOOT_ARGS
    boot
}
EOF
    
    print_success "Configuración GRUB creada"
}

# Función para crear estructura de ISO
create_iso_structure() {
    print_info "Creando estructura de ISO..."
    
    # Crear directorios
    mkdir -p iso-custom/boot
    
    # Copiar kernel
    cp "kernel-$ARCH-$TARGET.bin" "iso-custom/boot/"
    
    # Crear configuración GRUB
    create_custom_grub_config
    
    # Copiar archivos adicionales si existen
    if [ -n "$CONFIG_FILE" ] && [ -f "$CONFIG_FILE" ]; then
        print_info "Copiando archivo de configuración personalizada..."
        cp "$CONFIG_FILE" "iso-custom/"
    fi
    
    print_success "Estructura de ISO creada"
}

# Función para generar ISO
generate_iso() {
    print_info "Generando ISO: $OUTPUT_NAME"
    
    if [ "$DEBUG_MODE" = true ]; then
        grub-mkrescue -o "$OUTPUT_NAME" iso-custom
    else
        grub-mkrescue -o "$OUTPUT_NAME" iso-custom > /dev/null 2>&1
    fi
    
    if [ $? -eq 0 ]; then
        print_success "ISO generada: $OUTPUT_NAME"
        
        # Mostrar información del archivo
        local size=$(du -h "$OUTPUT_NAME" | cut -f1)
        print_info "Tamaño del ISO: $size"
    else
        print_error "Error generando ISO"
        exit 1
    fi
}

# Función para limpiar archivos temporales
cleanup() {
    print_info "Limpiando archivos temporales..."
    rm -rf iso-custom
    print_success "Limpieza completada"
}

# Función principal
main() {
    echo "============================================================"
    echo "IR0 Kernel - Creador de ISOs personalizadas"
    echo "============================================================"
    echo ""
    print_info "Configuración:"
    echo "  Arquitectura: $ARCH"
    echo "  Build Target: $TARGET"
    echo "  Archivo de salida: $OUTPUT_NAME"
    if [ -n "$BOOT_ARGS" ]; then
        echo "  Argumentos de boot: $BOOT_ARGS"
    fi
    if [ -n "$CONFIG_FILE" ]; then
        echo "  Archivo de configuración: $CONFIG_FILE"
    fi
    echo ""
    
    # Verificar que estamos en el directorio correcto
    if [ ! -f "Makefile" ]; then
        print_error "No se encontró el Makefile. Ejecute este script desde el directorio raíz del kernel."
        exit 1
    fi
    
    # Verificar dependencias
    check_dependencies
    
    # Compilar kernel
    compile_kernel
    
    # Crear estructura de ISO
    create_iso_structure
    
    # Generar ISO
    generate_iso
    
    # Limpiar archivos temporales
    cleanup
    
    print_success "ISO personalizada creada exitosamente: $OUTPUT_NAME"
}

# Ejecutar script principal
main "$@"
