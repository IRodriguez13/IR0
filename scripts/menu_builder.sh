#!/bin/bash

# IR0 Kernel - Menú interactivo de compilación
# Autor: IR0 Kernel Team
# Versión: 1.0

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

print_header() 
{
    clear
    echo -e "${CYAN}============================================================${NC}"
    echo -e "${CYAN}           IR0 Kernel - Sistema de Compilación${NC}"
    echo -e "${CYAN}============================================================${NC}"
    echo ""
}

print_info() 
{
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() 
{
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() 
{
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() 
{
    echo -e "${RED}[ERROR]${NC} $1"
}

# Función para mostrar el menú principal
show_main_menu() 
{
    print_header
    echo -e "${YELLOW}Menú Principal:${NC}"
    echo ""
    echo "1. Compilar kernel (arquitectura específica)"
    echo "2. Compilar todas las arquitecturas"
    echo "3. Compilar todos los build targets"
    echo "4. Compilar todas las combinaciones"
    echo "5. Crear ISO personalizada"
    echo "6. Ejecutar kernel en QEMU"
    echo "7. Limpiar archivos de compilación"
    echo "8. Ver información del sistema"
    echo "9. Salir"
    echo ""
    echo -n "Seleccione una opción: "
}

# Función para mostrar menú de arquitecturas
show_arch_menu() 
{
    echo ""
    echo -e "${YELLOW}Seleccione arquitectura:${NC}"
    echo "1. x86-32 (32-bit)"
    echo "2. x86-64 (64-bit)"
    echo "3. ARM32 (ARMv7)"
    echo "4. ARM64 (ARMv8)"
    echo "5. Volver al menú principal"
    echo ""
    echo -n "Opción: "
}

# Función para mostrar menú de build targets
show_target_menu() 
{
    echo ""
    echo -e "${YELLOW}Seleccione build target:${NC}"
    echo "1. Desktop (Sistema de escritorio)"
    echo "2. Server (Sistema servidor)"
    echo "3. IoT (Sistema IoT)"
    echo "4. Embedded (Sistema embebido)"
    echo "5. Volver al menú principal"
    echo ""
    echo -n "Opción: "
}

# Función para compilar kernel específico
compile_specific_kernel() 
{
    local arch=""
    local target=""
    
    # Seleccionar arquitectura
    while true; do
        show_arch_menu
        read -r arch_choice
        
        case $arch_choice in
            1) arch="x86-32"; break ;;
            2) arch="x86-64"; break ;;
            3) arch="arm32"; break ;;
            4) arch="arm64"; break ;;
            5) return ;;
            *) print_error "Opción no válida"; continue ;;
        esac
    done
    
    # Seleccionar build target
    while true; do
        show_target_menu
        read -r target_choice
        
        case $target_choice in
            1) target="desktop"; break ;;
            2) target="server"; break ;;
            3) target="iot"; break ;;
            4) target="embedded"; break ;;
            5) return ;;
            *) print_error "Opción no válida"; continue ;;
        esac
    done
    
    print_info "Compilando kernel para $arch-$target..."
    
    if make ARCH=$arch BUILD_TARGET=$target; then
        print_success "Kernel compilado exitosamente"
    else
        print_error "Error en la compilación"
    fi
    
    echo ""
    echo -n "Presione Enter para continuar..."
    read -r
}

# Función para compilar todas las arquitecturas
compile_all_architectures() 
{
    print_info "Compilando para todas las arquitecturas..."
    
    if ./scripts/build_all.sh -a; then
        print_success "Compilación completada"
    else
        print_error "Error en la compilación"
    fi
    
    echo ""
    echo -n "Presione Enter para continuar..."
    read -r
}

# Función para compilar todos los build targets
compile_all_targets() 
{
    print_info "Compilando para todos los build targets..."
    
    if ./scripts/build_all.sh -t; then
        print_success "Compilación completada"
    else
        print_error "Error en la compilación"
    fi
    
    echo ""
    echo -n "Presione Enter para continuar..."
    read -r
}

# Función para compilar todas las combinaciones
compile_all_combinations() 
{
    print_info "Compilando todas las combinaciones..."
    
    if ./scripts/build_all.sh -c; then
        print_success "Compilación completada"
    else
        print_error "Error en la compilación"
    fi
    
    echo ""
    echo -n "Presione Enter para continuar..."
    read -r
}

# Función para crear ISO personalizada
create_custom_iso() 
{
    local arch=""
    local target=""
    local output_name=""
    local boot_args=""
    
    # Seleccionar arquitectura
    while true; do
        show_arch_menu
        read -r arch_choice
        
        case $arch_choice in
            1) arch="x86-32"; break ;;
            2) arch="x86-64"; break ;;
            3) arch="arm32"; break ;;
            4) arch="arm64"; break ;;
            5) return ;;
            *) print_error "Opción no válida"; continue ;;
        esac
    done
    
    # Seleccionar build target
    while true; do
        show_target_menu
        read -r target_choice
        
        case $target_choice in
            1) target="desktop"; break ;;
            2) target="server"; break ;;
            3) target="iot"; break ;;
            4) target="embedded"; break ;;
            5) return ;;
            *) print_error "Opción no válida"; continue ;;
        esac
    done
    
    # Solicitar nombre de salida
    echo ""
    echo -n "Nombre del archivo ISO (Enter para usar por defecto): "
    read -r output_name
    
    # Solicitar argumentos de boot
    echo -n "Argumentos de boot adicionales (Enter para ninguno): "
    read -r boot_args
    
    # Construir comando
    local cmd="./scripts/create_iso.sh -a $arch -t $target"
    if [ -n "$output_name" ]; then
        cmd="$cmd -o $output_name"
    fi
    if [ -n "$boot_args" ]; then
        cmd="$cmd -b '$boot_args'"
    fi
    
    print_info "Creando ISO personalizada..."
    
    if eval $cmd; then
        print_success "ISO creada exitosamente"
    else
        print_error "Error creando ISO"
    fi
    
    echo ""
    echo -n "Presione Enter para continuar..."
    read -r
}

# Función para ejecutar en QEMU
run_qemu() 
{
    local arch=""
    local target=""
    
    # Seleccionar arquitectura
    while true; do
        show_arch_menu
        read -r arch_choice
        
        case $arch_choice in
            1) arch="x86-32"; break ;;
            2) arch="x86-64"; break ;;
            3) arch="arm32"; break ;;
            4) arch="arm64"; break ;;
            5) return ;;
            *) print_error "Opción no válida"; continue ;;
        esac
    done
    
    # Seleccionar build target
    while true; do
        show_target_menu
        read -r target_choice
        
        case $target_choice in
            1) target="desktop"; break ;;
            2) target="server"; break ;;
            3) target="iot"; break ;;
            4) target="embedded"; break ;;
            5) return ;;
            *) print_error "Opción no válida"; continue ;;
        esac
    done
    
    # Verificar que existe el kernel
    if [ ! -f "kernel-$arch-$target.bin" ]; then
        print_warning "Kernel no encontrado. ¿Desea compilarlo primero? (y/n): "
        read -r compile_choice
        if [ "$compile_choice" = "y" ] || [ "$compile_choice" = "Y" ]; then
            if make ARCH=$arch BUILD_TARGET=$target; then
                print_success "Kernel compilado"
            else
                print_error "Error compilando kernel"
                echo ""
                echo -n "Presione Enter para continuar..."
                read -r
                return
            fi
        else
            return
        fi
    fi
    
    print_info "Ejecutando kernel $arch-$target en QEMU..."
    
    if make ARCH=$arch BUILD_TARGET=$target run; then
        print_success "QEMU iniciado"
    else
        print_error "Error iniciando QEMU"
    fi
    
    echo ""
    echo -n "Presione Enter para continuar..."
    read -r
}

# Función para limpiar archivos
clean_files() 
{
    print_info "Limpiando archivos de compilación..."
    
    if make clean-all; then
        print_success "Limpieza completada"
    else
        print_error "Error en la limpieza"
    fi
    
    echo ""
    echo -n "Presione Enter para continuar..."
    read -r
}

# Función para mostrar información del sistema
show_system_info() 
{
    print_header
    echo -e "${YELLOW}Información del Sistema:${NC}"
    echo ""
    
    echo "Sistema operativo: $(uname -s)"
    echo "Arquitectura: $(uname -m)"
    echo "Versión del kernel: $(uname -r)"
    echo ""
    
    echo -e "${YELLOW}Dependencias:${NC}"
    echo -n "make: "
    if command -v make &> /dev/null; then
        echo -e "${GREEN}✓ Instalado${NC}"
    else
        echo -e "${RED}✗ No instalado${NC}"
    fi
    
    echo -n "gcc: "
    if command -v gcc &> /dev/null; then
        echo -e "${GREEN}✓ Instalado${NC}"
    else
        echo -e "${RED}✗ No instalado${NC}"
    fi
    
    echo -n "nasm: "
    if command -v nasm &> /dev/null; then
        echo -e "${GREEN}✓ Instalado${NC}"
    else
        echo -e "${RED}✗ No instalado${NC}"
    fi
    
    echo -n "grub-mkrescue: "
    if command -v grub-mkrescue &> /dev/null; then
        echo -e "${GREEN}✓ Instalado${NC}"
    else
        echo -e "${RED}✗ No instalado${NC}"
    fi
    
    echo -n "qemu-system-x86_64: "
    if command -v qemu-system-x86_64 &> /dev/null; then
        echo -e "${GREEN}✓ Instalado${NC}"
    else
        echo -e "${RED}✗ No instalado${NC}"
    fi
    
    echo -n "qemu-system-i386: "
    if command -v qemu-system-i386 &> /dev/null; then
        echo -e "${GREEN}✓ Instalado${NC}"
    else
        echo -e "${RED}✗ No instalado${NC}"
    fi
    
    echo ""
    echo -e "${YELLOW}Archivos del kernel:${NC}"
    ls -la kernel-*.bin kernel-*.iso 2>/dev/null || echo "No se encontraron archivos del kernel"
    
    echo ""
    echo -n "Presione Enter para continuar..."
    read -r
}

# Función principal del menú
main_menu() 
{
    while true; do
        show_main_menu
        read -r choice
        
        case $choice in
            1) compile_specific_kernel ;;
            2) compile_all_architectures ;;
            3) compile_all_targets ;;
            4) compile_all_combinations ;;
            5) create_custom_iso ;;
            6) run_qemu ;;
            7) clean_files ;;
            8) show_system_info ;;
            9) 
                print_info "¡Hasta luego!"
                exit 0
                ;;
            *) 
                print_error "Opción no válida"
                echo ""
                echo -n "Presione Enter para continuar..."
                read -r
                ;;
        esac
    done
}

# Verificar que estamos en el directorio correcto
if [ ! -f "Makefile" ]; then
    print_error "No se encontró el Makefile. Ejecute este script desde el directorio raíz del kernel."
    exit 1
fi

# Iniciar menú principal
main_menu
