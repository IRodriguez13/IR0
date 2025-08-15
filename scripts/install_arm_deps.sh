#!/bin/bash

# IR0 Kernel - Script para instalar dependencias ARM
# Autor: IR0 Kernel Team
# Versión: 1.0

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
    echo "IR0 Kernel - Instalador de dependencias ARM"
    echo ""
    echo "Uso: $0 [OPCIONES]"
    echo ""
    echo "Opciones:"
    echo "  -a, --all        Instalar todas las dependencias ARM"
    echo "  -c, --compiler   Instalar solo compiladores ARM"
    echo "  -q, --qemu       Instalar solo QEMU ARM"
    echo "  -h, --help       Mostrar esta ayuda"
    echo ""
    echo "Ejemplos:"
    echo "  $0 -a              # Instalar todo"
    echo "  $0 -c              # Solo compiladores"
    echo "  $0 -q              # Solo QEMU"
    echo ""
}

# Variables
INSTALL_COMPILERS=false
INSTALL_QEMU=false

# Procesar argumentos
while [[ $# -gt 0 ]]; do
    case $1 in
        -a|--all)
            INSTALL_COMPILERS=true
            INSTALL_QEMU=true
            shift
            ;;
        -c|--compiler)
            INSTALL_COMPILERS=true
            shift
            ;;
        -q|--qemu)
            INSTALL_QEMU=true
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

# Si no se especificó nada, instalar todo
if [ "$INSTALL_COMPILERS" = false ] && [ "$INSTALL_QEMU" = false ]; then
    INSTALL_COMPILERS=true
    INSTALL_QEMU=true
fi

# Función para detectar el sistema operativo
detect_os() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        OS=$NAME
        VER=$VERSION_ID
    else
        OS=$(uname -s)
        VER=$(uname -r)
    fi
}

# Función para instalar en Ubuntu/Debian
install_ubuntu() {
    print_info "Detectado Ubuntu/Debian. Instalando dependencias..."
    
    # Actualizar repositorios
    print_info "Actualizando repositorios..."
    sudo apt-get update
    
    if [ "$INSTALL_COMPILERS" = true ]; then
        print_info "Instalando compiladores ARM..."
        sudo apt-get install -y gcc-aarch64-linux-gnu gcc-arm-linux-gnueabi
        sudo apt-get install -y g++-aarch64-linux-gnu g++-arm-linux-gnueabi
        sudo apt-get install -y binutils-aarch64-linux-gnu binutils-arm-linux-gnueabi
    fi
    
    if [ "$INSTALL_QEMU" = true ]; then
        print_info "Instalando QEMU ARM..."
        sudo apt-get install -y qemu-system-arm qemu-system-aarch64
    fi
    
    print_success "Dependencias instaladas en Ubuntu/Debian"
}

# Función para instalar en Fedora
install_fedora() {
    print_info "Detectado Fedora. Instalando dependencias..."
    
    if [ "$INSTALL_COMPILERS" = true ]; then
        print_info "Instalando compiladores ARM..."
        sudo dnf install -y gcc-aarch64-linux-gnu gcc-arm-linux-gnu
        sudo dnf install -y gcc-c++-aarch64-linux-gnu gcc-c++-arm-linux-gnu
        sudo dnf install -y binutils-aarch64-linux-gnu binutils-arm-linux-gnu
    fi
    
    if [ "$INSTALL_QEMU" = true ]; then
        print_info "Instalando QEMU ARM..."
        sudo dnf install -y qemu-system-arm qemu-system-aarch64
    fi
    
    print_success "Dependencias instaladas en Fedora"
}

# Función para instalar en Arch Linux
install_arch() {
    print_info "Detectado Arch Linux. Instalando dependencias..."
    
    if [ "$INSTALL_COMPILERS" = true ]; then
        print_info "Instalando compiladores ARM..."
        sudo pacman -S --noconfirm aarch64-linux-gnu-gcc arm-linux-gnueabi-gcc
        sudo pacman -S --noconfirm aarch64-linux-gnu-binutils arm-linux-gnueabi-binutils
    fi
    
    if [ "$INSTALL_QEMU" = true ]; then
        print_info "Instalando QEMU ARM..."
        sudo pacman -S --noconfirm qemu-arch-extra
    fi
    
    print_success "Dependencias instaladas en Arch Linux"
}

# Función para verificar instalación
verify_installation() {
    print_info "Verificando instalación..."
    
    local missing_tools=()
    
    if [ "$INSTALL_COMPILERS" = true ]; then
        if ! command -v aarch64-linux-gnu-gcc &> /dev/null; then
            missing_tools+=("aarch64-linux-gnu-gcc")
        fi
        if ! command -v arm-linux-gnueabi-gcc &> /dev/null; then
            missing_tools+=("arm-linux-gnueabi-gcc")
        fi
    fi
    
    if [ "$INSTALL_QEMU" = true ]; then
        if ! command -v qemu-system-aarch64 &> /dev/null; then
            missing_tools+=("qemu-system-aarch64")
        fi
        if ! command -v qemu-system-arm &> /dev/null; then
            missing_tools+=("qemu-system-arm")
        fi
    fi
    
    if [ ${#missing_tools[@]} -eq 0 ]; then
        print_success "Todas las herramientas están instaladas correctamente"
        return 0
    else
        print_warning "Algunas herramientas no se instalaron: ${missing_tools[*]}"
        return 1
    fi
}

# Función para mostrar información post-instalación
show_post_install_info() {
    echo ""
    print_info "Información post-instalación:"
    echo ""
    echo "Compiladores ARM disponibles:"
    echo "  ARM64: aarch64-linux-gnu-gcc"
    echo "  ARM32: arm-linux-gnueabi-gcc"
    echo ""
    echo "QEMU ARM disponible:"
    echo "  ARM64: qemu-system-aarch64"
    echo "  ARM32: qemu-system-arm"
    echo ""
    echo "Para compilar para ARM:"
    echo "  make ARCH=arm64 BUILD_TARGET=desktop"
    echo "  make ARCH=arm32 BUILD_TARGET=desktop"
    echo ""
    echo "Para ejecutar en QEMU:"
    echo "  make ARCH=arm64 run"
    echo "  make ARCH=arm32 run"
    echo ""
}

# Función principal
main() {
    echo "============================================================"
    echo "IR0 Kernel - Instalador de Dependencias ARM"
    echo "============================================================"
    echo ""
    
    # Verificar si es root
    if [ "$EUID" -eq 0 ]; then
        print_error "No ejecute este script como root"
        exit 1
    fi
    
    # Detectar sistema operativo
    detect_os
    print_info "Sistema operativo detectado: $OS"
    
    # Instalar según el sistema operativo
    case $OS in
        *"Ubuntu"*|*"Debian"*|*"Linux Mint"*)
            install_ubuntu
            ;;
        *"Fedora"*|*"Red Hat"*|*"CentOS"*)
            install_fedora
            ;;
        *"Arch"*|*"Manjaro"*)
            install_arch
            ;;
        *)
            print_error "Sistema operativo no soportado: $OS"
            print_info "Instale manualmente:"
            echo "  - gcc-aarch64-linux-gnu"
            echo "  - gcc-arm-linux-gnueabi"
            echo "  - qemu-system-arm"
            echo "  - qemu-system-aarch64"
            exit 1
            ;;
    esac
    
    # Verificar instalación
    if verify_installation; then
        show_post_install_info
        print_success "Instalación completada exitosamente"
    else
        print_warning "Instalación completada con advertencias"
        show_post_install_info
    fi
}

# Ejecutar script principal
main "$@"
