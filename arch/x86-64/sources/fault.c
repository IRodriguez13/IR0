#include <stdint.h>
#include "../../includes/ir0/print.h"

// Función para delay simple
static void delay_ms(uint32_t ms)
{
    for (volatile uint32_t i = 0; i < ms * 100000; i++)
    {
        __asm__ volatile("nop");
    }
}

// Manejadores de excepciones básicos para x86-64
void page_fault_handler_x64(uint64_t error_code, uint64_t fault_address)
{
    print_colored("PAGE FAULT x86-64!\n", 0x0C, 0x00);
    delay_ms(1000);

    print("Error code: ");
    print_hex_compact(error_code);
    print("\nFault address: ");
    print_hex_compact(fault_address);
    print("\n");
    delay_ms(2000);

    // Mostrar información adicional
    print("PAGE FAULT DETALLES:\n");
    if (error_code & 1)
    {
        print("  - Presente: SI\n");
    }
    else
    {
        print("  - Presente: NO\n");
    }

    if (error_code & 2)
    {
        print("  - Escritura: SI\n");
    }
    else
    {
        print("  - Escritura: NO\n");
    }

    if (error_code & 4)
    {
        print("  - Usuario: SI\n");
    }
    else
    {
        print("  - Usuario: NO\n");
    }

    if (error_code & 8)
    {
        print("  - Reserved: SI\n");
    }
    else
    {
        print("  - Reserved: NO\n");
    }

    if (error_code & 16)
    {
        print("  - Instruction: SI\n");
    }
    else
    {
        print("  - Instruction: NO\n");
    }

    delay_ms(3000);

    // Halt el sistema
    while (1)
    {
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
