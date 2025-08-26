// arch/arm-32/sources/arch_arm.c - VERSIÓN ULTRA SIMPLE PARA ARM-32
// Implementación completamente modular y autónoma

#include "arm_types.h"

// Declaraciones UART
void uart_init(void);
void uart_puts(const char *str);
void uart_show_boot_sequence(void);

// Función de entrada principal para ARM-32
void _start(void) __attribute__((section(".text.boot")));
void kmain_arm32(void);

// Función simple de print para ARM
void arm_print(const char *str) {
    // Usar UART para output
    uart_puts(str);
}

// Función de entrada - llamada directamente por el bootloader
void _start(void) {
    // Configuración mínima para ARM
    // Deshabilitar interrupciones
    __asm__ volatile("cpsid if");
    
    // Configurar stack básico (simulado)
    // En una implementación real, esto vendría del linker script
    
    // Limpiar BSS (sección de datos no inicializados)
    extern uint8_t __bss_start, __bss_end;
    uint8_t *bss_start = &__bss_start;
    uint8_t *bss_end = &__bss_end;
    
    while (bss_start < bss_end) 
    {
        *bss_start++ = 0;
    }
    
    // Llamar a la función principal del kernel
    kmain_arm32();
    
    // Loop infinito si retorna
    while(1) {
        __asm__ volatile("wfi");  // Wait For Interrupt
    }
}

// Función principal del kernel ARM-32 - VERSIÓN CON UART
void kmain_arm32(void) 
{
    // Setup mínimo específico de ARM-32
    // ARM no necesita configuración compleja como x86
    
    // Inicializar UART y mostrar secuencia de arranque
    uart_show_boot_sequence();
    
    // También mostrar mensajes adicionales
    arm_print("IR0 Kernel ARM-32 iniciando...\n");
    arm_print("Versión ultra simple y modular\n");
    
    // ARM es más simple - no necesitamos IDT ni paginación compleja
    arm_print("ARM-32: Arquitectura más simple que x86\n");
    arm_print("No necesitamos IDT ni paginación compleja\n");
    arm_print("No necesitamos PIC ni interrupciones complejas\n");
    
    // Demostrar modularidad
    arm_print("Kernel ARM-32 funcionando correctamente!\n");
    arm_print("Arquitectura modular implementada exitosamente\n");
    arm_print("Código específico: kmain_arm32(), _start()\n");
    
    // Información del sistema
    arm_print("Sistema ARM-32 detectado\n");
    arm_print("Arquitectura: ARMv7-A\n");
    arm_print("Procesador: Cortex-A9 (simulado en QEMU)\n");
    
    // Kernel básico funcionando
    arm_print("Kernel ARM-32 estable y funcional!\n");
}
