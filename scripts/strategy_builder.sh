#!/bin/bash
# IR0 Kernel - Strategy Builder Script
# Script para manejar estrategias de compilación según el caso de uso

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
    echo -e "${CYAN}IR0 Kernel - Strategy Builder${NC}"
    echo ""
    echo "Uso: $0 [OPCIONES]"
    echo ""
    echo "OPCIONES:"
    echo "  -a, --arch ARCH        Arquitectura (x86-32, x86-64, arm32, arm64)"
    echo "  -s, --strategy STRAT   Estrategia de compilación"
    echo "  -c, --clean            Limpiar antes de compilar"
    echo "  -d, --debug            Compilar con debug"
    echo "  -r, --run              Ejecutar QEMU automáticamente"
    echo "  -l, --logs             Mostrar logs QEMU"
    echo "  -i, --info             Mostrar información de estrategia"
    echo "  -h, --help             Mostrar esta ayuda"
    echo ""
    echo "ESTRATEGIAS DISPONIBLES:"
    echo "  desktop   - Sistema de escritorio completo (GUI, audio, USB, networking)"
    echo "  server    - Servidor de alto rendimiento (networking, SSL, virtualización)"
    echo "  iot       - Sistema IoT ligero (power management, timers de baja potencia)"
    echo "  embedded  - Sistema embebido mínimo (sin GUI ni networking)"
    echo ""
    echo "EJEMPLOS:"
    echo "  $0 -a x86-64 -s desktop    Compilar kernel desktop 64-bit"
    echo "  $0 -a x86-32 -s server     Compilar kernel server 32-bit"
    echo "  $0 -a x86-64 -s iot -r     Compilar y ejecutar kernel IoT"
    echo "  $0 -s desktop -i           Mostrar información de estrategia desktop"
}

# Función para mostrar información de estrategia
show_strategy_info() {
    local strategy=$1
    
    echo -e "${CYAN}========================================${NC}"
    echo -e "${CYAN}    INFORMACIÓN DE ESTRATEGIA: $strategy${NC}"
    echo -e "${CYAN}========================================${NC}"
    echo ""
    
    case $strategy in
        "desktop")
            echo -e "${GREEN}🎯 Estrategia: Desktop${NC}"
            echo -e "${BLUE}Descripción: Sistema de escritorio completo con GUI, audio y multimedia${NC}"
            echo ""
            echo -e "${YELLOW}Características habilitadas:${NC}"
            echo "  ✅ GUI (Interfaz gráfica)"
            echo "  ✅ Audio (Sistema de sonido)"
            echo "  ✅ USB (Dispositivos USB)"
            echo "  ✅ Networking (Redes)"
            echo "  ✅ Filesystem (Sistema de archivos)"
            echo "  ✅ Multimedia (Multimedia)"
            echo "  ✅ Printing (Impresión)"
            echo "  ✅ VFS (Sistema de archivos virtual)"
            echo "  ✅ TCP/IP (Protocolos de red)"
            echo "  ✅ Sockets (Comunicación)"
            echo "  ✅ Ethernet (Red Ethernet)"
            echo "  ✅ User Mode (Modo usuario)"
            echo "  ✅ Memory Protection (Protección de memoria)"
            echo "  ✅ Process Isolation (Aislamiento de procesos)"
            echo ""
            echo -e "${YELLOW}Configuración de memoria:${NC}"
            echo "  📊 Heap Size: 256MB"
            echo "  📊 Max Processes: 1024"
            echo "  📊 Max Threads: 4096"
            echo "  📊 Scheduler Quantum: 10ms"
            echo "  📊 IO Buffer Size: 64KB"
            ;;
        "server")
            echo -e "${GREEN}🎯 Estrategia: Server${NC}"
            echo -e "${BLUE}Descripción: Servidor de alto rendimiento con networking y virtualización${NC}"
            echo ""
            echo -e "${YELLOW}Características habilitadas:${NC}"
            echo "  ❌ GUI (Sin interfaz gráfica)"
            echo "  ❌ Audio (Sin sistema de sonido)"
            echo "  ✅ USB (Dispositivos USB)"
            echo "  ✅ Networking (Redes)"
            echo "  ✅ Filesystem (Sistema de archivos)"
            echo "  ❌ Multimedia (Sin multimedia)"
            echo "  ❌ Printing (Sin impresión)"
            echo "  ✅ VFS (Sistema de archivos virtual)"
            echo "  ✅ TCP/IP (Protocolos de red)"
            echo "  ✅ Sockets (Comunicación)"
            echo "  ✅ Ethernet (Red Ethernet)"
            echo "  ✅ User Mode (Modo usuario)"
            echo "  ✅ Memory Protection (Protección de memoria)"
            echo "  ✅ Process Isolation (Aislamiento de procesos)"
            echo "  ✅ Network Security (Seguridad de red)"
            echo ""
            echo -e "${YELLOW}Configuración de memoria:${NC}"
            echo "  📊 Heap Size: 1GB"
            echo "  📊 Max Processes: 4096"
            echo "  📊 Max Threads: 16384"
            echo "  📊 Scheduler Quantum: 5ms"
            echo "  📊 IO Buffer Size: 256KB"
            ;;
        "iot")
            echo -e "${GREEN}🎯 Estrategia: IoT${NC}"
            echo -e "${BLUE}Descripción: Sistema IoT ligero con power management${NC}"
            echo ""
            echo -e "${YELLOW}Características habilitadas:${NC}"
            echo "  ❌ GUI (Sin interfaz gráfica)"
            echo "  ❌ Audio (Sin sistema de sonido)"
            echo "  ❌ USB (Sin dispositivos USB)"
            echo "  ✅ Networking (Redes)"
            echo "  ✅ Filesystem (Sistema de archivos)"
            echo "  ❌ Multimedia (Sin multimedia)"
            echo "  ❌ Printing (Sin impresión)"
            echo "  ✅ VFS (Sistema de archivos virtual)"
            echo "  ✅ TCP/IP (Protocolos de red)"
            echo "  ✅ Sockets (Comunicación)"
            echo "  ✅ Ethernet (Red Ethernet)"
            echo "  ❌ User Mode (Sin modo usuario)"
            echo "  ✅ Memory Protection (Protección de memoria)"
            echo "  ❌ Process Isolation (Sin aislamiento de procesos)"
            echo "  ✅ Power Management (Gestión de energía)"
            echo "  ✅ Sleep Modes (Modos de sueño)"
            echo "  ✅ Low Power Timers (Timers de baja potencia)"
            echo ""
            echo -e "${YELLOW}Configuración de memoria:${NC}"
            echo "  📊 Heap Size: 16MB"
            echo "  📊 Max Processes: 64"
            echo "  📊 Max Threads: 256"
            echo "  📊 Scheduler Quantum: 20ms"
            echo "  📊 IO Buffer Size: 4KB"
            ;;
        "embedded")
            echo -e "${GREEN}🎯 Estrategia: Embedded${NC}"
            echo -e "${BLUE}Descripción: Sistema embebido mínimo sin GUI ni networking${NC}"
            echo ""
            echo -e "${YELLOW}Características habilitadas:${NC}"
            echo "  ❌ GUI (Sin interfaz gráfica)"
            echo "  ❌ Audio (Sin sistema de sonido)"
            echo "  ❌ USB (Sin dispositivos USB)"
            echo "  ❌ Networking (Sin redes)"
            echo "  ❌ Filesystem (Sin sistema de archivos)"
            echo "  ❌ Multimedia (Sin multimedia)"
            echo "  ❌ Printing (Sin impresión)"
            echo "  ❌ VFS (Sin sistema de archivos virtual)"
            echo "  ❌ TCP/IP (Sin protocolos de red)"
            echo "  ❌ Sockets (Sin comunicación)"
            echo "  ❌ Ethernet (Sin red Ethernet)"
            echo "  ❌ User Mode (Sin modo usuario)"
            echo "  ❌ Memory Protection (Sin protección de memoria)"
            echo "  ❌ Process Isolation (Sin aislamiento de procesos)"
            echo "  ✅ Power Management (Gestión de energía)"
            echo "  ✅ Sleep Modes (Modos de sueño)"
            echo "  ✅ Low Power Timers (Timers de baja potencia)"
            echo ""
            echo -e "${YELLOW}Configuración de memoria:${NC}"
            echo "  📊 Heap Size: 4MB"
            echo "  📊 Max Processes: 16"
            echo "  📊 Max Threads: 64"
            echo "  📊 Scheduler Quantum: 50ms"
            echo "  📊 IO Buffer Size: 1KB"
            ;;
        *)
            echo -e "${RED}❌ Estrategia '$strategy' no válida${NC}"
            return 1
            ;;
    esac
    
    echo ""
    echo -e "${CYAN}========================================${NC}"
}

# Función para validar estrategia
validate_strategy() {
    local strategy=$1
    case $strategy in
        "desktop"|"server"|"iot"|"embedded")
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

# Función para validar arquitectura
validate_arch() {
    local arch=$1
    case $arch in
        "x86-32"|"x86-64"|"arm32"|"arm64")
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

# Función para compilar con estrategia
compile_with_strategy() {
    local arch=$1
    local strategy=$2
    local clean=$3
    local debug=$4
    
    echo -e "${CYAN}========================================${NC}"
    echo -e "${CYAN}    COMPILANDO CON ESTRATEGIA${NC}"
    echo -e "${CYAN}========================================${NC}"
    echo -e "${BLUE}Arquitectura: $arch${NC}"
    echo -e "${BLUE}Estrategia: $strategy${NC}"
    echo -e "${BLUE}Limpiar: $clean${NC}"
    echo -e "${BLUE}Debug: $debug${NC}"
    echo ""
    
    # Limpiar si se solicita
    if [[ "$clean" == "true" ]]; then
        echo -e "${YELLOW}🔨 Limpiando archivos de compilación...${NC}"
        make ARCH=$arch BUILD_TARGET=$strategy clean
        echo ""
    fi
    
    # Compilar el kernel
    echo -e "${YELLOW}🚀 Compilando kernel $arch-$strategy...${NC}"
    
    # Configurar flags de debug si es necesario
    local debug_flags=""
    if [[ "$debug" == "true" ]]; then
        debug_flags="CFLAGS_DEBUG=-g"
    fi
    
    if make ARCH=$arch BUILD_TARGET=$strategy $debug_flags kernel-$arch-$strategy.iso; then
        echo ""
        echo -e "${GREEN}✅ Kernel compilado exitosamente!${NC}"
        echo -e "${GREEN}📁 Archivo: kernel-$arch-$strategy.iso${NC}"
        echo ""
        
        # Mostrar información del archivo
        if [[ -f "kernel-$arch-$strategy.iso" ]]; then
            echo -e "${BLUE}📊 Información del archivo:${NC}"
            ls -lh kernel-$arch-$strategy.iso
            echo ""
        fi
        
        return 0
    else
        echo ""
        echo -e "${RED}❌ Error en la compilación${NC}"
        echo -e "${YELLOW}💡 Verifique los errores arriba${NC}"
        return 1
    fi
}

# Función para ejecutar en QEMU
run_in_qemu() {
    local arch=$1
    local strategy=$2
    local show_logs=$3
    
    echo -e "${CYAN}🚀 Ejecutando QEMU automáticamente...${NC}"
    
    # Generar nombre único para el log
    local timestamp=$(date +"%Y%m%d_%H%M%S")
    local log_file="qemu_log_${arch}_${strategy}_${timestamp}.log"
    
    echo -e "${BLUE}📝 Log QEMU: $log_file${NC}"
    echo ""
    
    # Ejecutar QEMU con flags específicas según arquitectura
    case $arch in
        "x86-64")
            echo -e "${YELLOW}🐛 Ejecutando QEMU x86-64...${NC}"
            qemu-system-x86_64 \
                -cdrom kernel-$arch-$strategy.iso \
                -m 512M \
                -cpu qemu64,+apic \
                -smp 2 \
                -machine q35 \
                -no-reboot \
                -no-shutdown \
                -display gtk \
                -d int,cpu_reset \
                -D "$log_file" &
            ;;
        "x86-32")
            echo -e "${YELLOW}🐛 Ejecutando QEMU x86-32...${NC}"
            qemu-system-i386 \
                -cdrom kernel-$arch-$strategy.iso \
                -m 512M \
                -cpu qemu32,+apic \
                -machine q35 \
                -no-reboot \
                -no-shutdown \
                -display gtk \
                -d int,cpu_reset \
                -D "$log_file" &
            ;;
        "arm64")
            echo -e "${YELLOW}🐛 Ejecutando QEMU ARM64...${NC}"
            qemu-system-aarch64 \
                -M virt \
                -cpu cortex-a57 \
                -m 512M \
                -kernel kernel-$arch-$strategy.bin \
                -no-reboot \
                -no-shutdown \
                -display gtk \
                -d int,cpu_reset \
                -D "$log_file" &
            ;;
        "arm32")
            echo -e "${YELLOW}🐛 Ejecutando QEMU ARM32...${NC}"
            qemu-system-arm \
                -M vexpress-a9 \
                -cpu cortex-a9 \
                -m 512M \
                -kernel kernel-$arch-$strategy.bin \
                -no-reboot \
                -no-shutdown \
                -display gtk \
                -d int,cpu_reset \
                -D "$log_file" &
            ;;
    esac
    
    local qemu_pid=$!
    echo -e "${BLUE}🔄 QEMU ejecutándose (PID: $qemu_pid)${NC}"
    echo -e "${BLUE}⏱️  Esperando 15 segundos para recopilar logs...${NC}"
    
    # Esperar 15 segundos para que QEMU genere logs
    sleep 15
    
    # Terminar QEMU si está ejecutándose
    if kill -0 $qemu_pid 2>/dev/null; then
        echo -e "${YELLOW}🛑 Terminando QEMU...${NC}"
        kill $qemu_pid
        wait $qemu_pid 2>/dev/null
    fi
    
    echo ""
    echo -e "${GREEN}✅ QEMU terminado${NC}"
    
    # Mostrar logs si se solicita
    if [[ "$show_logs" == "true" && -f "$log_file" ]]; then
        echo ""
        echo -e "${CYAN}📋 LOGS QEMU:${NC}"
        echo -e "${CYAN}========================================${NC}"
        cat "$log_file"
        echo -e "${CYAN}========================================${NC}"
        echo ""
        echo -e "${BLUE}💾 Log completo guardado en: $log_file${NC}"
    elif [[ -f "$log_file" ]]; then
        echo -e "${BLUE}💾 Log guardado en: $log_file${NC}"
        echo -e "${BLUE}💡 Use -l para mostrar los logs${NC}"
    fi
}

# Variables por defecto
ARCH=$DEFAULT_ARCH
STRATEGY=$DEFAULT_STRATEGY
CLEAN=false
DEBUG=false
AUTO_RUN=false
SHOW_LOGS=false
SHOW_INFO=false

# Parsear argumentos
while [[ $# -gt 0 ]]; do
    case $1 in
        -a|--arch)
            ARCH="$2"
            shift 2
            ;;
        -s|--strategy)
            STRATEGY="$2"
            shift 2
            ;;
        -c|--clean)
            CLEAN=true
            shift
            ;;
        -d|--debug)
            DEBUG=true
            shift
            ;;
        -r|--run)
            AUTO_RUN=true
            shift
            ;;
        -l|--logs)
            SHOW_LOGS=true
            shift
            ;;
        -i|--info)
            SHOW_INFO=true
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

# Validar arquitectura
if ! validate_arch "$ARCH"; then
    echo -e "${RED}Error: Arquitectura '$ARCH' no válida. Use x86-32, x86-64, arm32, o arm64${NC}"
    exit 1
fi

# Validar estrategia
if ! validate_strategy "$STRATEGY"; then
    echo -e "${RED}Error: Estrategia '$STRATEGY' no válida. Use desktop, server, iot, o embedded${NC}"
    exit 1
fi

# Mostrar información de estrategia si se solicita
if [[ "$SHOW_INFO" == "true" ]]; then
    show_strategy_info "$STRATEGY"
    exit 0
fi

# Mostrar información inicial
echo -e "${MAGENTA}========================================${NC}"
echo -e "${MAGENTA}    IR0 KERNEL - STRATEGY BUILDER    ${NC}"
echo -e "${MAGENTA}========================================${NC}"
echo -e "${BLUE}Arquitectura: $ARCH${NC}"
echo -e "${BLUE}Estrategia: $STRATEGY${NC}"
echo -e "${BLUE}Limpiar: $CLEAN${NC}"
echo -e "${BLUE}Debug: $DEBUG${NC}"
echo -e "${BLUE}Auto-run: $AUTO_RUN${NC}"
echo -e "${BLUE}Mostrar logs: $SHOW_LOGS${NC}"
echo ""

# Compilar con estrategia
if compile_with_strategy "$ARCH" "$STRATEGY" "$CLEAN" "$DEBUG"; then
    # Ejecutar automáticamente si se solicita
    if [[ "$AUTO_RUN" == "true" ]]; then
        run_in_qemu "$ARCH" "$STRATEGY" "$SHOW_LOGS"
    else
        # Preguntar si quiere ejecutar
        echo -e "${CYAN}¿Desea ejecutar el kernel? (y/n)${NC}"
        read -r response
        if [[ "$response" =~ ^[Yy]$ ]]; then
            run_in_qemu "$ARCH" "$STRATEGY" "$SHOW_LOGS"
        fi
    fi
    
    exit 0
else
    exit 1
fi
