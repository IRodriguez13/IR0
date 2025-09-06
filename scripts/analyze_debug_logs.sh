#!/bin/bash

# IR0 Kernel Debug Log Analyzer
# Analizador de logs de debugging del kernel IR0

set -e

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Función para mostrar ayuda
show_help() 
{
    echo -e "${BLUE}IR0 Kernel Debug Log Analyzer${NC}"
    echo ""
    echo "Uso: $0 [OPCIONES] [ARCHIVO_LOG]"
    echo ""
    echo "Opciones:"
    echo "  -p, --page-faults      Analizar page faults"
    echo "  -i, --interrupts       Analizar interrupciones"
    echo "  -e, --execution        Analizar ejecución de instrucciones"
    echo "  -g, --guest-errors     Analizar errores del guest"
    echo "  -a, --all              Analizar todo (por defecto)"
    echo "  -s, --summary          Mostrar solo resumen"
    echo "  -h, --help             Mostrar esta ayuda"
    echo ""
    echo "Ejemplos:"
    echo "  $0 qemu_debug.log              # Analizar todo"
    echo "  $0 -p qemu_debug.log           # Solo page faults"
    echo "  $0 -i -s qemu_debug.log        # Solo interrupciones (resumen)"
    echo "  $0 -a qemu_debug.log           # Analizar todo"
    echo ""
}

# Variables por defecto
LOG_FILE="qemu_debug.log"
ANALYZE_PAGE_FAULTS=true
ANALYZE_INTERRUPTS=true
ANALYZE_EXECUTION=true
ANALYZE_GUEST_ERRORS=true
SHOW_SUMMARY=false

# Parsear argumentos
while [[ $# -gt 0 ]]; do
    case $1 in
        -p|--page-faults)
            ANALYZE_PAGE_FAULTS=true
            ANALYZE_INTERRUPTS=false
            ANALYZE_EXECUTION=false
            ANALYZE_GUEST_ERRORS=false
            shift
            ;;
        -i|--interrupts)
            ANALYZE_PAGE_FAULTS=false
            ANALYZE_INTERRUPTS=true
            ANALYZE_EXECUTION=false
            ANALYZE_GUEST_ERRORS=false
            shift
            ;;
        -e|--execution)
            ANALYZE_PAGE_FAULTS=false
            ANALYZE_INTERRUPTS=false
            ANALYZE_EXECUTION=true
            ANALYZE_GUEST_ERRORS=false
            shift
            ;;
        -g|--guest-errors)
            ANALYZE_PAGE_FAULTS=false
            ANALYZE_INTERRUPTS=false
            ANALYZE_EXECUTION=false
            ANALYZE_GUEST_ERRORS=true
            shift
            ;;
        -a|--all)
            ANALYZE_PAGE_FAULTS=true
            ANALYZE_INTERRUPTS=true
            ANALYZE_EXECUTION=true
            ANALYZE_GUEST_ERRORS=true
            shift
            ;;
        -s|--summary)
            SHOW_SUMMARY=true
            shift
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        -*)
            echo -e "${RED}Error: Opción desconocida $1${NC}"
            show_help
            exit 1
            ;;
        *)
            LOG_FILE="$1"
            shift
            ;;
    esac
done

# Función para verificar si el archivo existe
check_file() 
{
    if [ ! -f "$LOG_FILE" ]; then
        echo -e "${RED}Error: Archivo de log no encontrado: $LOG_FILE${NC}"
        echo -e "${YELLOW}Tip: Ejecuta el kernel con debugging primero:${NC}"
        echo -e "  ./scripts/quick_run.sh -64 -d"
        echo -e "  make run-64-debug"
        exit 1
    fi
}

# Función para mostrar información del archivo
show_file_info() 
{
    echo -e "${BLUE}📊 Analizando archivo: ${YELLOW}$LOG_FILE${NC}"
    echo -e "Tamaño: ${CYAN}$(du -h "$LOG_FILE" | cut -f1)${NC}"
    echo -e "Líneas: ${CYAN}$(wc -l < "$LOG_FILE")${NC}"
    echo -e "Última modificación: ${CYAN}$(stat -c %y "$LOG_FILE")${NC}"
    echo ""
}

# Función para analizar page faults
analyze_page_faults() 
{
    if [ "$ANALYZE_PAGE_FAULTS" = true ]; then
        echo -e "${YELLOW}🔍 Analizando Page Faults...${NC}"
        
        local page_faults=$(grep -i "page fault\|paging\|memory" "$LOG_FILE" | wc -l)
        local specific_faults=$(grep -i "page fault" "$LOG_FILE" | wc -l)
        
        if [ "$SHOW_SUMMARY" = true ]; then
            echo -e "  Page faults totales: ${CYAN}$page_faults${NC}"
            echo -e "  Page faults específicos: ${CYAN}$specific_faults${NC}"
        else
            echo -e "  Page faults totales: ${CYAN}$page_faults${NC}"
            echo -e "  Page faults específicos: ${CYAN}$specific_faults${NC}"
            
            if [ $specific_faults -gt 0 ]; then
                echo -e "${RED}  ⚠️  Page faults detectados:${NC}"
                grep -i "page fault" "$LOG_FILE" | head -10 | while read line; do
                    echo -e "    ${RED}$line${NC}"
                done
            else
                echo -e "${GREEN}  ✅ No se detectaron page faults específicos${NC}"
            fi
        fi
        echo ""
    fi
}

# Función para analizar interrupciones
analyze_interrupts() 
{
    if [ "$ANALYZE_INTERRUPTS" = true ]; then
        echo -e "${YELLOW}🔍 Analizando Interrupciones...${NC}"
        
        local interrupts=$(grep -i "interrupt\|int\|irq" "$LOG_FILE" | wc -l)
        local cpu_resets=$(grep -i "reset\|cpu" "$LOG_FILE" | wc -l)
        
        if [ "$SHOW_SUMMARY" = true ]; then
            echo -e "  Interrupciones totales: ${CYAN}$interrupts${NC}"
            echo -e "  CPU resets: ${CYAN}$cpu_resets${NC}"
        else
            echo -e "  Interrupciones totales: ${CYAN}$interrupts${NC}"
            echo -e "  CPU resets: ${CYAN}$cpu_resets${NC}"
            
            if [ $interrupts -gt 0 ]; then
                echo -e "${BLUE}  📋 Últimas interrupciones:${NC}"
                grep -i "interrupt\|int" "$LOG_FILE" | tail -5 | while read line; do
                    echo -e "    ${BLUE}$line${NC}"
                done
            fi
        fi
        echo ""
    fi
}

# Función para analizar ejecución
analyze_execution() 
{
    if [ "$ANALYZE_EXECUTION" = true ]; then
        echo -e "${YELLOW}🔍 Analizando Ejecución...${NC}"
        
        local exec_lines=$(grep -i "exec\|instruction" "$LOG_FILE" | wc -l)
        local jumps=$(grep -i "jmp\|jump\|call" "$LOG_FILE" | wc -l)
        
        if [ "$SHOW_SUMMARY" = true ]; then
            echo -e "  Líneas de ejecución: ${CYAN}$exec_lines${NC}"
            echo -e "  Saltos/llamadas: ${CYAN}$jumps${NC}"
        else
            echo -e "  Líneas de ejecución: ${CYAN}$exec_lines${NC}"
            echo -e "  Saltos/llamadas: ${CYAN}$jumps${NC}"
            
            if [ $exec_lines -gt 0 ]; then
                echo -e "${GREEN}  📋 Últimas instrucciones ejecutadas:${NC}"
                grep -i "exec" "$LOG_FILE" | tail -5 | while read line; do
                    echo -e "    ${GREEN}$line${NC}"
                done
            fi
        fi
        echo ""
    fi
}

# Función para analizar errores del guest
analyze_guest_errors() 
{
    if [ "$ANALYZE_GUEST_ERRORS" = true ]; then
        echo -e "${YELLOW}🔍 Analizando Errores del Guest...${NC}"
        
        local guest_errors=$(grep -i "guest.*error\|error.*guest" "$LOG_FILE" | wc -l)
        local general_errors=$(grep -i "error\|exception\|fault" "$LOG_FILE" | wc -l)
        
        if [ "$SHOW_SUMMARY" = true ]; then
            echo -e "  Errores del guest: ${CYAN}$guest_errors${NC}"
            echo -e "  Errores generales: ${CYAN}$general_errors${NC}"
        else
            echo -e "  Errores del guest: ${CYAN}$guest_errors${NC}"
            echo -e "  Errores generales: ${CYAN}$general_errors${NC}"
            
            if [ $guest_errors -gt 0 ]; then
                echo -e "${RED}  ⚠️  Errores del guest detectados:${NC}"
                grep -i "guest.*error\|error.*guest" "$LOG_FILE" | head -10 | while read line; do
                    echo -e "    ${RED}$line${NC}"
                done
            else
                echo -e "${GREEN}  ✅ No se detectaron errores del guest${NC}"
            fi
            
            if [ $general_errors -gt 0 ]; then
                echo -e "${YELLOW}  ⚠️  Errores generales detectados:${NC}"
                grep -i "error\|exception\|fault" "$LOG_FILE" | head -5 | while read line; do
                    echo -e "    ${YELLOW}$line${NC}"
                done
            fi
        fi
        echo ""
    fi
}

# Función para mostrar resumen general
show_general_summary() 
{
    echo -e "${BLUE}📈 Resumen General${NC}"
    
    local total_lines=$(wc -l < "$LOG_FILE")
    local error_lines=$(grep -i "error\|fault\|exception" "$LOG_FILE" | wc -l)
    local warning_lines=$(grep -i "warning" "$LOG_FILE" | wc -l)
    
    echo -e "  Total de líneas: ${CYAN}$total_lines${NC}"
    echo -e "  Líneas con errores: ${CYAN}$error_lines${NC}"
    echo -e "  Líneas con warnings: ${CYAN}$warning_lines${NC}"
    
    if [ $error_lines -eq 0 ]; then
        echo -e "${GREEN}  ✅ No se detectaron errores${NC}"
    else
        echo -e "${RED}  ⚠️  Se detectaron $error_lines errores${NC}"
    fi
    
    echo ""
}

# Función principal
main() 
{
    check_file
    show_file_info
    
    if [ "$SHOW_SUMMARY" = true ]; then
        show_general_summary
    fi
    
    analyze_page_faults
    analyze_interrupts
    analyze_execution
    analyze_guest_errors
    
    if [ "$SHOW_SUMMARY" = false ]; then
        show_general_summary
    fi
    
    echo -e "${GREEN}✅ Análisis completado${NC}"
}

# Ejecutar función principal
main
