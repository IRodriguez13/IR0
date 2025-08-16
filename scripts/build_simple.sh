#!/bin/bash
# Script de compilación simplificado para IR0 Kernel

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Función para mostrar ayuda
show_help() {
    echo "Uso: $0 [OPCIONES]"
    echo ""
    echo "OPCIONES:"
    echo "  -a, --arch ARCH    Arquitectura (x86-32, x86-64)"
    echo "  -t, --target TARGET Build target (desktop, server, iot, embedded)"
    echo "  -c, --clean        Limpiar antes de compilar"
    echo "  -d, --debug        Compilar con debug"
    echo "  -r, --run          Ejecutar QEMU automáticamente"
    echo "  -l, --logs         Mostrar logs QEMU después de ejecutar"
    echo "  -h, --help         Mostrar esta ayuda"
    echo ""
    echo "EJEMPLOS:"
    echo "  $0 -a x86-32       Compilar kernel 32-bit"
    echo "  $0 -a x86-64 -t desktop  Compilar kernel 64-bit desktop"
    echo "  $0 -a x86-32 -c    Limpiar y compilar 32-bit"
    echo "  $0 -a x86-64 -r    Compilar y ejecutar automáticamente"
    echo "  $0 -a x86-64 -r -l Compilar, ejecutar y mostrar logs"
}

# Variables por defecto
ARCH="x86-32"
TARGET="desktop"
CLEAN=false
DEBUG=false
AUTO_RUN=false
SHOW_LOGS=false

# Parsear argumentos
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

# Verificar que la arquitectura sea válida
if [[ "$ARCH" != "x86-32" && "$ARCH" != "x86-64" ]]; then
    echo -e "${RED}Error: Arquitectura '$ARCH' no válida. Use x86-32 o x86-64${NC}"
    exit 1
fi

# Verificar que el target sea válido
if [[ "$TARGET" != "desktop" && "$TARGET" != "server" && "$TARGET" != "iot" && "$TARGET" != "embedded" ]]; then
    echo -e "${RED}Error: Build target '$TARGET' no válido. Use desktop, server, iot, o embedded${NC}"
    exit 1
fi

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}    IR0 KERNEL - COMPILACIÓN SIMPLE    ${NC}"
echo -e "${CYAN}========================================${NC}"
echo -e "${BLUE}Arquitectura: $ARCH${NC}"
echo -e "${BLUE}Build Target: $TARGET${NC}"
echo -e "${BLUE}Limpiar: $CLEAN${NC}"
echo -e "${BLUE}Debug: $DEBUG${NC}"
echo -e "${BLUE}Auto-run: $AUTO_RUN${NC}"
echo -e "${BLUE}Mostrar logs: $SHOW_LOGS${NC}"
echo ""

# Limpiar si se solicita
if [[ "$CLEAN" == "true" ]]; then
    echo -e "${YELLOW}🔨 Limpiando archivos de compilación...${NC}"
    make ARCH=$ARCH BUILD_TARGET=$TARGET clean
    echo ""
fi

# Compilar el kernel
echo -e "${YELLOW}🚀 Compilando kernel $ARCH-$TARGET...${NC}"
if make ARCH=$ARCH BUILD_TARGET=$TARGET kernel-$ARCH-$TARGET.iso; then
    echo ""
    echo -e "${GREEN}✅ Kernel compilado exitosamente!${NC}"
    echo -e "${GREEN}📁 Archivo: kernel-$ARCH-$TARGET.iso${NC}"
    echo -e "${GREEN}📁 Enlace: kernel-$ARCH.iso${NC}"
    echo ""
    
    # Mostrar información del archivo
    if [[ -f "kernel-$ARCH-$TARGET.iso" ]]; then
        echo -e "${BLUE}📊 Información del archivo:${NC}"
        ls -lh kernel-$ARCH-$TARGET.iso
        echo ""
    fi
    
    # Ejecutar automáticamente si se solicita
    if [[ "$AUTO_RUN" == "true" ]]; then
        echo -e "${CYAN}🚀 Ejecutando QEMU automáticamente...${NC}"
        
        # Generar nombre único para el log
        TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
        LOG_FILE="qemu_log_${ARCH}_${TARGET}_${TIMESTAMP}.log"
        
        echo -e "${BLUE}📝 Log QEMU: $LOG_FILE${NC}"
        echo ""
        
        # Ejecutar QEMU con flags específicas
        if [[ "$ARCH" == "x86-64" ]]; then
            echo -e "${YELLOW}🐛 Ejecutando QEMU x86-64 con debug...${NC}"
            qemu-system-x86_64 \
                -cdrom kernel-$ARCH-$TARGET.iso \
                -m 512M \
                -display gtk \
                -d int,cpu_reset \
                -D "$LOG_FILE" &
        else
            echo -e "${YELLOW}🐛 Ejecutando QEMU x86-32 con debug...${NC}"
            qemu-system-i386 \
                -cdrom kernel-$ARCH-$TARGET.iso \
                -m 512M \
                -display gtk \
                -d int,cpu_reset \
                -D "$LOG_FILE" &
        fi
        
        QEMU_PID=$!
        echo -e "${BLUE}🔄 QEMU ejecutándose (PID: $QEMU_PID)${NC}"
        echo -e "${BLUE}⏱️  Esperando 15 segundos para recopilar logs...${NC}"
        
        # Esperar 15 segundos para que QEMU genere logs
        sleep 15
        
        # Terminar QEMU si está ejecutándose
        if kill -0 $QEMU_PID 2>/dev/null; then
            echo -e "${YELLOW}🛑 Terminando QEMU...${NC}"
            kill $QEMU_PID
            wait $QEMU_PID 2>/dev/null
        fi
        
        echo ""
        echo -e "${GREEN}✅ QEMU terminado${NC}"
        
        # Mostrar logs si se solicita
        if [[ "$SHOW_LOGS" == "true" && -f "$LOG_FILE" ]]; then
            echo ""
            echo -e "${CYAN}📋 LOGS QEMU:${NC}"
            echo -e "${CYAN}========================================${NC}"
            cat "$LOG_FILE"
            echo -e "${CYAN}========================================${NC}"
            echo ""
            echo -e "${BLUE}💾 Log completo guardado en: $LOG_FILE${NC}"
        elif [[ -f "$LOG_FILE" ]]; then
            echo -e "${BLUE}💾 Log guardado en: $LOG_FILE${NC}"
            echo -e "${BLUE}💡 Use -l para mostrar los logs${NC}"
        fi
        
    else
        # Preguntar si quiere ejecutar (comportamiento original)
        echo -e "${CYAN}¿Desea ejecutar el kernel? (y/n)${NC}"
        read -r response
        if [[ "$response" =~ ^[Yy]$ ]]; then
            if [[ "$DEBUG" == "true" ]]; then
                echo -e "${YELLOW}🐛 Ejecutando con debug...${NC}"
                make ARCH=$ARCH BUILD_TARGET=$TARGET debug
            else
                echo -e "${YELLOW}▶️  Ejecutando kernel...${NC}"
                make ARCH=$ARCH BUILD_TARGET=$TARGET run
            fi
        fi
    fi
    
    exit 0
else
    echo ""
    echo -e "${RED}❌ Error en la compilación${NC}"
    echo -e "${YELLOW}💡 Verifique los errores arriba${NC}"
    exit 1
fi
