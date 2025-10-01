#include <stdint.h>
#include <ir0/print.h>
#include <panic/panic.h>

// Manejadores de excepciones básicos para x86-64
static char pf_log_buffer[256] __attribute__((unused));
static int pf_log_pos __attribute__((unused)) = 0;

void page_fault_handler_x64(uint64_t error_code __attribute__((unused)), uint64_t fault_address)
{
    // CRITICAL: NO CALLS TO ANYTHING - Direct VGA write to avoid recursion
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    const char *msg = "PAGE FAULT!";

    // Clear screen with red background
    for (int i = 0; i < 80 * 25; i++)
    {
        vga[i] = 0x4F20; // White on red, space
    }

    // Write message
    for (int i = 0; msg[i] != '\0'; i++)
    {
        vga[i] = 0x4F00 | msg[i]; // White on red
    }

    // Write fault address in hex (simple)
    vga[80] = 0x4F00 | 'A';
    vga[81] = 0x4F00 | 'd';
    vga[82] = 0x4F00 | 'd';
    vga[83] = 0x4F00 | 'r';
    vga[84] = 0x4F00 | ':';
    vga[85] = 0x4F00 | ' ';

    // Write address (simplified - just show it's not zero)
    if (fault_address)
    {
        vga[86] = 0x4F00 | 'N';
        vga[87] = 0x4F00 | 'O';
        vga[88] = 0x4F00 | 'N';
        vga[89] = 0x4F00 | 'Z';
    }
    else
    {
        vga[86] = 0x4F00 | 'Z';
        vga[87] = 0x4F00 | 'E';
        vga[88] = 0x4F00 | 'R';
        vga[89] = 0x4F00 | 'O';
    }

    // Halt forever - NO RETURN
    __asm__ volatile("cli");
    for (;;)
        __asm__ volatile("hlt");
}

void general_protection_fault_x64(uint64_t error_code)
{
    (void)error_code;
    panic("GPF");
    (void)error_code;
    panic("GPF");
}

void double_fault_x64(uint64_t error_code)
{
    (void)error_code;
    for (;;)
        __asm__ volatile("cli; hlt");
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
    print("TRIPLE FAULT - Error FATAL del sistema!\n");
    panic("Triple FAULT");
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
