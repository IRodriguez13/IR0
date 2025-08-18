#!/bin/bash
# Script para analizar logs de QEMU y encontrar errores comunes

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Función para mostrar ayuda
show_help() {
    echo "Uso: $0 [OPCIONES] <archivo_log>"
    echo ""
    echo "OPCIONES:"
    echo "  -a, --all          Mostrar todos los tipos de errores"
    echo "  -t, --triple       Buscar triple faults"
    echo "  -p, --page         Buscar page faults"
    echo "  -i, --interrupt    Buscar errores de interrupciones"
    echo "  -c, --cpu          Buscar errores de CPU"
    echo "  -g, --guest        Buscar errores de guest"
    echo "  -h, --help         Mostrar esta ayuda"
    echo ""
    echo "EJEMPLOS:"
    echo "  $0 qemu_log_x86-64_desktop_20240817_210500.log"
    echo "  $0 -a qemu_debug_x86-64_desktop_20240817_210500.log"
    echo "  $0 -t -p qemu_log_x86-64_desktop_20240817_210500.log"
}

# Variables
LOG_FILE=""
SHOW_ALL=false
SHOW_TRIPLE=false
SHOW_PAGE=false
SHOW_INTERRUPT=false
SHOW_CPU=false
SHOW_GUEST=false

# Parsear argumentos
while [[ $# -gt 0 ]]; do
    case $1 in
        -a|--all)
            SHOW_ALL=true
            shift
            ;;
        -t|--triple)
            SHOW_TRIPLE=true
            shift
            ;;
        -p|--page)
            SHOW_PAGE=true
            shift
            ;;
        -i|--interrupt)
            SHOW_INTERRUPT=true
            shift
            ;;
        -c|--cpu)
            SHOW_CPU=true
            shift
            ;;
        -g|--guest)
            SHOW_GUEST=true
            shift
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        *)
            if [[ -z "$LOG_FILE" ]]; then
                LOG_FILE="$1"
            else
                echo -e "${RED}Error: Solo se puede especificar un archivo de log${NC}"
                exit 1
            fi
            shift
            ;;
    esac
done

# Verificar que se proporcionó un archivo de log
if [[ -z "$LOG_FILE" ]]; then
    echo -e "${RED}Error: Debe especificar un archivo de log${NC}"
    show_help
    exit 1
fi

# Verificar que el archivo existe
if [[ ! -f "$LOG_FILE" ]]; then
    echo -e "${RED}Error: El archivo '$LOG_FILE' no existe${NC}"
    exit 1
fi

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}    ANALIZADOR DE LOGS QEMU - IR0      ${NC}"
echo -e "${CYAN}========================================${NC}"
echo -e "${BLUE}Archivo: $LOG_FILE${NC}"
echo -e "${BLUE}Tamaño: $(ls -lh "$LOG_FILE" | awk '{print $5}')${NC}"
echo ""

# Si no se especificó ningún tipo, mostrar todos
if [[ "$SHOW_ALL" == "false" && "$SHOW_TRIPLE" == "false" && "$SHOW_PAGE" == "false" && "$SHOW_INTERRUPT" == "false" && "$SHOW_CPU" == "false" && "$SHOW_GUEST" == "false" ]]; then
    SHOW_ALL=true
fi

# Función para buscar y mostrar resultados
search_and_show() {
    local pattern="$1"
    local description="$2"
    local color="$3"
    
    echo -e "${color}🔍 $description:${NC}"
    if grep -i "$pattern" "$LOG_FILE" > /dev/null; then
        echo -e "${RED}❌ ENCONTRADO:${NC}"
        grep -i "$pattern" "$LOG_FILE" | head -10
        local count=$(grep -i "$pattern" "$LOG_FILE" | wc -l)
        if [[ $count -gt 10 ]]; then
            echo -e "${YELLOW}... y $((count - 10)) líneas más${NC}"
        fi
    else
        echo -e "${GREEN}✅ No encontrado${NC}"
    fi
    echo ""
}

# Buscar diferentes tipos de errores
if [[ "$SHOW_ALL" == "true" || "$SHOW_TRIPLE" == "true" ]]; then
    search_and_show "triple fault" "Triple Faults" "$RED"
fi

if [[ "$SHOW_ALL" == "true" || "$SHOW_PAGE" == "true" ]]; then
    search_and_show "page fault" "Page Faults" "$YELLOW"
fi

if [[ "$SHOW_ALL" == "true" || "$SHOW_INTERRUPT" == "true" ]]; then
    search_and_show "interrupt" "Interrupciones" "$BLUE"
fi

if [[ "$SHOW_ALL" == "true" || "$SHOW_CPU" == "true" ]]; then
    search_and_show "cpu_reset" "Errores de CPU" "$CYAN"
fi

if [[ "$SHOW_ALL" == "true" || "$SHOW_GUEST" == "true" ]]; then
    search_and_show "guest_errors" "Errores de Guest" "$MAGENTA"
fi

# Mostrar resumen
echo -e "${CYAN}📊 RESUMEN:${NC}"
echo -e "${BLUE}Total de líneas en el log: $(wc -l < "$LOG_FILE")${NC}"
echo -e "${BLUE}Triple faults: $(grep -i "triple fault" "$LOG_FILE" | wc -l)${NC}"
echo -e "${BLUE}Page faults: $(grep -i "page fault" "$LOG_FILE" | wc -l)${NC}"
echo -e "${BLUE}Interrupciones: $(grep -i "interrupt" "$LOG_FILE" | wc -l)${NC}"
echo -e "${BLUE}Errores de CPU: $(grep -i "cpu_reset" "$LOG_FILE" | wc -l)${NC}"
echo -e "${BLUE}Errores de guest: $(grep -i "guest_errors" "$LOG_FILE" | wc -l)${NC}"

echo ""
echo -e "${GREEN}✅ Análisis completado${NC}"
