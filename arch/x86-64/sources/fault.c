#include <stdint.h>
#include "../../includes/ir0/print.h"

// Manejadores de excepciones básicos para x86-64
// Buffer seguro para logging (en memoria estática)
static char pf_log_buffer[256];
static int pf_log_pos = 0;

void page_fault_handler_x64(uint64_t error_code, uint64_t fault_address)
{
    // ✅ SOLUCIÓN: Usar buffer estático en lugar de print/heap
    // Limpiar buffer
    pf_log_pos = 0;
    
    // Log básico sin funciones complejas
    const char* msg = "PAGE FAULT: ";
    for (int i = 0; msg[i] && pf_log_pos < 255; i++) {
        pf_log_buffer[pf_log_pos++] = msg[i];
    }
    
    // Convertir error_code a hex simple
    for (int i = 0; i < 16; i++) {
        uint8_t nibble = (error_code >> (60 - i * 4)) & 0xF;
        char hex_char = (nibble < 10) ? '0' + nibble : 'A' + (nibble - 10);
        if (pf_log_pos < 255) pf_log_buffer[pf_log_pos++] = hex_char;
    }
    
    // Agregar fault address
    msg = " FA: ";
    for (int i = 0; msg[i] && pf_log_pos < 255; i++) {
        pf_log_buffer[pf_log_pos++] = msg[i];
    }
    
    for (int i = 0; i < 16; i++) {
        uint8_t nibble = (fault_address >> (60 - i * 4)) & 0xF;
        char hex_char = (nibble < 10) ? '0' + nibble : 'A' + (nibble - 10);
        if (pf_log_pos < 255) pf_log_buffer[pf_log_pos++] = hex_char;
    }
    
    pf_log_buffer[pf_log_pos] = '\0';
    
    // ✅ SOLUCIÓN: Halt inmediato sin más operaciones
    while (1) {
        __asm__ volatile("hlt");
    }
}

void general_protection_fault_x64(uint64_t error_code)
{
    print_colored("GENERAL PROTECTION FAULT x86-64!\n", 0x0C, 0x00);
    delay_ms(1000);

    print("Error code: ");
    print_hex_compact(error_code);
    print("\n");
    delay_ms(2000);

    // Mostrar información adicional
    print("GP FAULT DETALLES:\n");
    if (error_code & 1)
    {
        print("  - External: SI\n");
    }
    else
    {
        print("  - External: NO\n");
    }

    if (error_code & 2)
    {
        print("  - Table: IDT\n");
    }
    else
    {
        print("  - Table: GDT/LDT\n");
    }

    uint16_t selector = (error_code >> 3) & 0xFFFF;
    print("  - Selector: ");
    print_hex_compact(selector);
    print("\n");

    delay_ms(3000);

    // Halt el sistema
    while (1)
    {
        __asm__ volatile("hlt");
    }
}

void double_fault_x64(uint64_t error_code)
{
    print_colored("DOUBLE FAULT x86-64!\n", 0x0C, 0x00);
    delay_ms(1000);

    print("Error code: ");
    print_hex_compact(error_code);
    print("\n");
    delay_ms(2000);

    print("DOUBLE FAULT - Error crítico del sistema!\n");
    print("Esto indica que una excepción ocurrió mientras se manejaba otra excepción.\n");
    delay_ms(3000);

    // Halt el sistema
    while (1)
    {
        __asm__ volatile("hlt");
    }
}

void triple_fault_x64()
{
    print_colored("TRIPLE FAULT x86-64!\n", 0x0C, 0x00);
    delay_ms(1000);

    print("TRIPLE FAULT - Error FATAL del sistema!\n");
    print("Esto causa un reset inmediato del CPU.\n");
    print("Posibles causas:\n");
    print("  1. IDT mal configurado\n");
    print("  2. PIC mal configurado\n");
    print("  3. Manejador de excepción corrupto\n");
    print("  4. Stack overflow\n");
    delay_ms(5000);

    // Triple fault causa reset automático, pero intentamos mostrar info
    while (1)
    {
        __asm__ volatile("hlt");
    }
}

void invalid_opcode_x64()
{
    print_colored("INVALID OPCODE x86-64!\n", 0x0C, 0x00);
    delay_ms(1000);

    print("Se intentó ejecutar una instrucción no válida\n");
    delay_ms(2000);

    // Halt el sistema
    while (1)
    {
        __asm__ volatile("hlt");
    }
}

void divide_by_zero_x64()
{
    print_colored("DIVIDE BY ZERO x86-64!\n", 0x0C, 0x00);
    delay_ms(1000);

    print("Se intentó dividir por cero\n");
    delay_ms(2000);

    // Halt el sistema
    while (1)
    {
        __asm__ volatile("hlt");
    }
}
