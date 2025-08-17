#!/bin/bash
# IR0 Kernel - Configuration Manager
# Script para gestionar la configuración del kernel y estrategias

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m' # No Color

# Configuración por defecto
DEFAULT_ARCH="x86-64"
DEFAULT_STRATEGY="desktop"

# Función para mostrar ayuda
show_help() {
    echo -e "${CYAN}IR0 Kernel - Configuration Manager${NC}"
    echo ""
    echo "Uso: $0 [COMANDO] [OPCIONES]"
    echo ""
    echo "COMANDOS:"
    echo "  build [OPCIONES]     Compilar kernel con estrategia específica"
    echo "  config [OPCIONES]    Gestionar configuración del kernel"
    echo "  info [OPCIONES]      Mostrar información de configuración"
    echo "  validate             Validar configuración actual"
    echo "  help                 Mostrar esta ayuda"
    echo ""
    echo "OPCIONES DE BUILD:"
    echo "  -a, --arch ARCH      Arquitectura (x86-32, x86-64, arm32, arm64)"
    echo "  -s, --strategy STRAT Estrategia (desktop, server, iot, embedded)"
    echo "  -c, --clean          Limpiar antes de compilar"
    echo "  -d, --debug          Compilar con debug"
    echo "  -r, --run            Ejecutar QEMU automáticamente"
    echo "  -l, --logs           Mostrar logs QEMU"
    echo ""
    echo "OPCIONES DE CONFIG:"
    echo "  --show-current       Mostrar configuración actual"
    echo "  --set-arch ARCH      Establecer arquitectura por defecto"
    echo "  --set-strategy STRAT Establecer estrategia por defecto"
    echo "  --reset              Restablecer configuración por defecto"
    echo ""
    echo "OPCIONES DE INFO:"
    echo "  --strategy STRAT     Mostrar información de estrategia específica"
    echo "  --arch ARCH          Mostrar información de arquitectura"
    echo "  --all                Mostrar toda la información"
    echo ""
    echo "EJEMPLOS:"
    echo "  $0 build -a x86-64 -s desktop    Compilar kernel desktop 64-bit"
    echo "  $0 config --show-current         Mostrar configuración actual"
    echo "  $0 info --strategy server        Mostrar info de estrategia server"
    echo "  $0 validate                       Validar configuración"
}

# Función para mostrar información de configuración
show_config_info() {
    echo -e "${CYAN}========================================${NC}"
    echo -e "${CYAN}    CONFIGURACIÓN ACTUAL DEL KERNEL${NC}"
    echo -e "${CYAN}========================================${NC}"
    echo ""
    
    # Leer configuración actual si existe
    if [[ -f ".kernel_config" ]]; then
        echo -e "${GREEN}📁 Archivo de configuración encontrado${NC}"
        echo ""
        cat .kernel_config
    else
        echo -e "${YELLOW}⚠️  No se encontró archivo de configuración${NC}"
        echo -e "${BLUE}Usando configuración por defecto:${NC}"
        echo "  Arquitectura: $DEFAULT_ARCH"
        echo "  Estrategia: $DEFAULT_STRATEGY"
    fi
    
    echo ""
    echo -e "${CYAN}========================================${NC}"
}

# Función para validar configuración
validate_config() {
    echo -e "${CYAN}========================================${NC}"
    echo -e "${CYAN}    VALIDANDO CONFIGURACIÓN${NC}"
    echo -e "${CYAN}========================================${NC}"
    echo ""
    
    local errors=0
    local warnings=0
    
    # Verificar archivos de configuración
    echo -e "${BLUE}🔍 Verificando archivos de configuración...${NC}"
    
    if [[ ! -f "setup/kernel_config.h" ]]; then
        echo -e "${RED}❌ Error: setup/kernel_config.h no encontrado${NC}"
        ((errors++))
    else
        echo -e "${GREEN}✅ setup/kernel_config.h encontrado${NC}"
    fi
    
    if [[ ! -f "setup/kernelconfig.h" ]]; then
        echo -e "${RED}❌ Error: setup/kernelconfig.h no encontrado${NC}"
        ((errors++))
    else
        echo -e "${GREEN}✅ setup/kernelconfig.h encontrado${NC}"
    fi
    
    if [[ ! -f "setup/kernel_config.c" ]]; then
        echo -e "${RED}❌ Error: setup/kernel_config.c no encontrado${NC}"
        ((errors++))
    else
        echo -e "${GREEN}✅ setup/kernel_config.c encontrado${NC}"
    fi
    
    # Verificar scripts
    echo ""
    echo -e "${BLUE}🔍 Verificando scripts...${NC}"
    
    if [[ ! -f "scripts/strategy_builder.sh" ]]; then
        echo -e "${RED}❌ Error: scripts/strategy_builder.sh no encontrado${NC}"
        ((errors++))
    else
        echo -e "${GREEN}✅ scripts/strategy_builder.sh encontrado${NC}"
    fi
    
    if [[ ! -x "scripts/strategy_builder.sh" ]]; then
        echo -e "${YELLOW}⚠️  Warning: scripts/strategy_builder.sh no es ejecutable${NC}"
        ((warnings++))
    else
        echo -e "${GREEN}✅ scripts/strategy_builder.sh es ejecutable${NC}"
    fi
    
    # Verificar Makefile
    echo ""
    echo -e "${BLUE}🔍 Verificando Makefile...${NC}"
    
    if [[ ! -f "Makefile" ]]; then
        echo -e "${RED}❌ Error: Makefile no encontrado${NC}"
        ((errors++))
    else
        echo -e "${GREEN}✅ Makefile encontrado${NC}"
    fi
    
    # Verificar dependencias
    echo ""
    echo -e "${BLUE}🔍 Verificando dependencias...${NC}"
    
    if ! command -v gcc &> /dev/null; then
        echo -e "${RED}❌ Error: gcc no encontrado${NC}"
        ((errors++))
    else
        echo -e "${GREEN}✅ gcc encontrado${NC}"
    fi
    
    if ! command -v nasm &> /dev/null; then
        echo -e "${RED}❌ Error: nasm no encontrado${NC}"
        ((errors++))
    else
        echo -e "${GREEN}✅ nasm encontrado${NC}"
    fi
    
    if ! command -v make &> /dev/null; then
        echo -e "${RED}❌ Error: make no encontrado${NC}"
        ((errors++))
    else
        echo -e "${GREEN}✅ make encontrado${NC}"
    fi
    
    if ! command -v qemu-system-x86_64 &> /dev/null; then
        echo -e "${YELLOW}⚠️  Warning: qemu-system-x86_64 no encontrado${NC}"
        ((warnings++))
    else
        echo -e "${GREEN}✅ qemu-system-x86_64 encontrado${NC}"
    fi
    
    # Resumen
    echo ""
    echo -e "${CYAN}========================================${NC}"
    echo -e "${CYAN}    RESUMEN DE VALIDACIÓN${NC}"
    echo -e "${CYAN}========================================${NC}"
    
    if [[ $errors -eq 0 ]]; then
        echo -e "${GREEN}✅ Configuración válida${NC}"
    else
        echo -e "${RED}❌ Se encontraron $errors errores${NC}"
    fi
    
    if [[ $warnings -gt 0 ]]; then
        echo -e "${YELLOW}⚠️  Se encontraron $warnings advertencias${NC}"
    fi
    
    echo -e "${CYAN}========================================${NC}"
    
    return $errors
}

# Función para gestionar configuración
manage_config() {
    local action=$1
    shift
    
    case $action in
        "--show-current")
            show_config_info
            ;;
        "--set-arch")
            local arch=$1
            if [[ -z "$arch" ]]; then
                echo -e "${RED}Error: Debe especificar una arquitectura${NC}"
                exit 1
            fi
            
            # Validar arquitectura
            case $arch in
                "x86-32"|"x86-64"|"arm32"|"arm64")
                    echo "ARCH=$arch" > .kernel_config
                    echo -e "${GREEN}✅ Arquitectura establecida: $arch${NC}"
                    ;;
                *)
                    echo -e "${RED}Error: Arquitectura '$arch' no válida${NC}"
                    exit 1
                    ;;
            esac
            ;;
        "--set-strategy")
            local strategy=$1
            if [[ -z "$strategy" ]]; then
                echo -e "${RED}Error: Debe especificar una estrategia${NC}"
                exit 1
            fi
            
            # Validar estrategia
            case $strategy in
                "desktop"|"server"|"iot"|"embedded")
                    echo "STRATEGY=$strategy" >> .kernel_config
                    echo -e "${GREEN}✅ Estrategia establecida: $strategy${NC}"
                    ;;
                *)
                    echo -e "${RED}Error: Estrategia '$strategy' no válida${NC}"
                    exit 1
                    ;;
            esac
            ;;
        "--reset")
            rm -f .kernel_config
            echo -e "${GREEN}✅ Configuración restablecida${NC}"
            ;;
        *)
            echo -e "${RED}Error: Opción de configuración '$action' no válida${NC}"
            show_help
            exit 1
            ;;
    esac
}

# Función para mostrar información
show_info() {
    local action=$1
    shift
    
    case $action in
        "--strategy")
            local strategy=$1
            if [[ -z "$strategy" ]]; then
                echo -e "${RED}Error: Debe especificar una estrategia${NC}"
                exit 1
            fi
            
            # Usar strategy_builder.sh para mostrar información
            ./scripts/strategy_builder.sh -s "$strategy" -i
            ;;
        "--arch")
            local arch=$1
            if [[ -z "$arch" ]]; then
                echo -e "${RED}Error: Debe especificar una arquitectura${NC}"
                exit 1
            fi
            
            echo -e "${CYAN}========================================${NC}"
            echo -e "${CYAN}    INFORMACIÓN DE ARQUITECTURA: $arch${NC}"
            echo -e "${CYAN}========================================${NC}"
            echo ""
            
            case $arch in
                "x86-32")
                    echo -e "${GREEN}🎯 Arquitectura: x86-32${NC}"
                    echo -e "${BLUE}Descripción: Procesador Intel/AMD de 32 bits${NC}"
                    echo ""
                    echo -e "${YELLOW}Características:${NC}"
                    echo "  📊 Registros: EAX, EBX, ECX, EDX, ESI, EDI, EBP, ESP"
                    echo "  📊 Espacio de direcciones: 4GB"
                    echo "  📊 Modo: Protected Mode"
                    echo "  📊 Paginación: 4KB pages"
                    echo "  📊 Compilador: gcc -m32"
                    echo "  📊 Ensamblador: nasm -f elf32"
                    echo "  📊 Linker: ld -m elf_i386"
                    ;;
                "x86-64")
                    echo -e "${GREEN}🎯 Arquitectura: x86-64${NC}"
                    echo -e "${BLUE}Descripción: Procesador Intel/AMD de 64 bits${NC}"
                    echo ""
                    echo -e "${YELLOW}Características:${NC}"
                    echo "  📊 Registros: RAX, RBX, RCX, RDX, RSI, RDI, RBP, RSP, R8-R15"
                    echo "  📊 Espacio de direcciones: 256TB"
                    echo "  📊 Modo: Long Mode"
                    echo "  📊 Paginación: 4KB, 2MB, 1GB pages"
                    echo "  📊 Compilador: gcc -m64"
                    echo "  📊 Ensamblador: nasm -f elf64"
                    echo "  📊 Linker: ld -m elf_x86_64"
                    ;;
                "arm32")
                    echo -e "${GREEN}🎯 Arquitectura: ARM32${NC}"
                    echo -e "${BLUE}Descripción: Procesador ARM de 32 bits${NC}"
                    echo ""
                    echo -e "${YELLOW}Características:${NC}"
                    echo "  📊 Registros: R0-R15"
                    echo "  📊 Espacio de direcciones: 4GB"
                    echo "  📊 Modo: ARMv7"
                    echo "  📊 Paginación: 4KB pages"
                    echo "  📊 Compilador: arm-linux-gnueabi-gcc"
                    echo "  📊 Ensamblador: arm-linux-gnueabi-as"
                    echo "  📊 Linker: arm-linux-gnueabi-ld"
                    ;;
                "arm64")
                    echo -e "${GREEN}🎯 Arquitectura: ARM64${NC}"
                    echo -e "${BLUE}Descripción: Procesador ARM de 64 bits${NC}"
                    echo ""
                    echo -e "${YELLOW}Características:${NC}"
                    echo "  📊 Registros: X0-X30, SP, PC"
                    echo "  📊 Espacio de direcciones: 256TB"
                    echo "  📊 Modo: ARMv8"
                    echo "  📊 Paginación: 4KB, 2MB, 1GB pages"
                    echo "  📊 Compilador: aarch64-linux-gnu-gcc"
                    echo "  📊 Ensamblador: aarch64-linux-gnu-as"
                    echo "  📊 Linker: aarch64-linux-gnu-ld"
                    ;;
                *)
                    echo -e "${RED}❌ Arquitectura '$arch' no válida${NC}"
                    exit 1
                    ;;
            esac
            
            echo ""
            echo -e "${CYAN}========================================${NC}"
            ;;
        "--all")
            echo -e "${CYAN}========================================${NC}"
            echo -e "${CYAN}    INFORMACIÓN COMPLETA DEL KERNEL${NC}"
            echo -e "${CYAN}========================================${NC}"
            echo ""
            
            # Información de configuración
            show_config_info
            
            # Información de estrategias
            echo -e "${BLUE}📋 Estrategias disponibles:${NC}"
            ./scripts/strategy_builder.sh -s desktop -i
            echo ""
            ./scripts/strategy_builder.sh -s server -i
            echo ""
            ./scripts/strategy_builder.sh -s iot -i
            echo ""
            ./scripts/strategy_builder.sh -s embedded -i
            
            # Información de arquitecturas
            echo -e "${BLUE}📋 Arquitecturas disponibles:${NC}"
            echo "  x86-32  - Procesador Intel/AMD de 32 bits"
            echo "  x86-64  - Procesador Intel/AMD de 64 bits"
            echo "  arm32   - Procesador ARM de 32 bits"
            echo "  arm64   - Procesador ARM de 64 bits"
            
            echo ""
            echo -e "${CYAN}========================================${NC}"
            ;;
        *)
            echo -e "${RED}Error: Opción de información '$action' no válida${NC}"
            show_help
            exit 1
            ;;
    esac
}

# Función para compilar
build_kernel() {
    # Pasar argumentos al strategy_builder.sh
    ./scripts/strategy_builder.sh "$@"
}

# Función principal
main() {
    local command=$1
    shift
    
    case $command in
        "build")
            build_kernel "$@"
            ;;
        "config")
            manage_config "$@"
            ;;
        "info")
            show_info "$@"
            ;;
        "validate")
            validate_config
            ;;
        "help"|"--help"|"-h")
            show_help
            ;;
        "")
            show_help
            ;;
        *)
            echo -e "${RED}Error: Comando '$command' no válido${NC}"
            show_help
            exit 1
            ;;
    esac
}

# Ejecutar función principal
main "$@"
