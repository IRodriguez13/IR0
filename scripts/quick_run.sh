#!/bin/bash

# IR0 Kernel Quick Run Script
# Script rápido para ejecutar el kernel IR0

set -e

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Función para mostrar ayuda
show_help() 
{
    echo -e "${YELLOW}IR0 Kernel Quick Run Script${NC}"
    echo ""
    echo "Uso: $0 [OPCIONES]"
    echo ""
    echo "Opciones:"
    echo "  -32, --32bit          Compilar y ejecutar 32-bit"
    echo "  -64, --64bit          Compilar y ejecutar 64-bit"
    echo "  -g, --gui             Ejecutar con interfaz gráfica (por defecto)"
    echo "  -n, --nographic       Ejecutar en terminal (sin GUI)"
    echo "  -t, --test            Ejecutar para testing (con timeout)"
    echo "  -d, --debug           Ejecutar con debugging completo"
    echo "  -c, --clean           Limpiar antes de compilar"
    echo "  -h, --help            Mostrar esta ayuda"
    echo ""
    echo "Ejemplos:"
    echo "  $0 -32 -g              # 32-bit con GUI"
    echo "  $0 -64 -n              # 64-bit sin GUI"
    echo "  $0 -32 -t              # 32-bit para testing"
    echo "  $0 -64 -d              # 64-bit con debugging"
    echo "  $0 -64 -c -g           # 64-bit, limpiar y ejecutar con GUI"
    echo "  $0 -32 -d -n           # 32-bit con debugging en terminal"
    echo ""
}

# Variables por defecto
ARCH="x86-32"
MODE="gui"
CLEAN=false
DEBUG=false

# Parsear argumentos
while [[ $# -gt 0 ]]; do
    case $1 in
        -32|--32bit)
            ARCH="x86-32"
            shift
            ;;
        -64|--64bit)
            ARCH="x86-64"
            shift
            ;;
        -g|--gui)
            MODE="gui"
            shift
            ;;
        -n|--nographic)
            MODE="nographic"
            shift
            ;;
        -t|--test)
            MODE="test"
            shift
            ;;
        -d|--debug)
            DEBUG=true
            shift
            ;;
        -c|--clean)
            CLEAN=true
            shift
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        *)
            echo -e "${RED}Error: Opción desconocida $1${NC}"
            show_help
            exit 1
            ;;
    esac
done

# Función para mostrar información
show_info() 
{
    echo -e "${BLUE}🎯 IR0 Kernel Quick Run${NC}"
    echo -e "Arquitectura: ${YELLOW}$ARCH${NC}"
    echo -e "Modo: ${YELLOW}$MODE${NC}"
    echo -e "Debugging: ${YELLOW}$DEBUG${NC}"
    echo -e "Limpieza: ${YELLOW}$CLEAN${NC}"
    echo ""
}

# Función para limpiar
clean_build() 
{
    if [ "$CLEAN" = true ]; then
        echo -e "${YELLOW}🧹 Limpiando build anterior...${NC}"
        make clean
        echo -e "${GREEN}✅ Limpieza completada${NC}"
        echo ""
    fi
}

# Función para compilar
compile_kernel() 
{
    echo -e "${YELLOW}🔨 Compilando kernel para $ARCH...${NC}"
    make ARCH=$ARCH
    echo -e "${GREEN}✅ Compilación completada${NC}"
    echo ""
}

# Función para ejecutar
run_kernel() 
{
    echo -e "${YELLOW}🚀 Ejecutando kernel...${NC}"
    
    if [ "$DEBUG" = true ]; then
        echo -e "${BLUE}🐛 Modo debugging activado - logs en qemu_debug.log${NC}"
        case $MODE in
            "gui")
                make ARCH=$ARCH run-debug
                ;;
            "nographic")
                make ARCH=$ARCH run-debug-nographic
                ;;
            "test")
                make ARCH=$ARCH run-test
                ;;
            *)
                echo -e "${RED}Error: Modo desconocido $MODE${NC}"
                exit 1
                ;;
        esac
    else
        case $MODE in
            "gui")
                make ARCH=$ARCH run-gui
                ;;
            "nographic")
                make ARCH=$ARCH run-nographic
                ;;
            "test")
                make ARCH=$ARCH run-test
                ;;
            *)
                echo -e "${RED}Error: Modo desconocido $MODE${NC}"
                exit 1
                ;;
        esac
    fi
}

# Función principal
main() 
{
    show_info
    clean_build
    compile_kernel
    run_kernel
}

# Ejecutar función principal
main
