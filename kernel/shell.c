// shell.c - Shell running in Ring 3 (user space)
#include <stdint.h>

// Keyboard buffer shared with kernel (in low memory, always mapped)
#define KEYBOARD_BUFFER_ADDR 0x500000  // 5MB mark - safe area
volatile char *shell_keyboard_buffer = (volatile char *)KEYBOARD_BUFFER_ADDR;
volatile int *shell_keyboard_buffer_pos = (volatile int *)(KEYBOARD_BUFFER_ADDR + 256);

// Direct VGA write for Ring 3 (no kernel functions)
static void shell_write_char(char c, int pos, uint8_t color)
{
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    vga[pos] = (color << 8) | c;
}

static void shell_write_vga(const char *msg, uint8_t color)
{
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    static int pos = 0;
    
    for (int i = 0; msg[i] != '\0'; i++) {
        if (msg[i] == '\n') {
            pos = (pos / 80 + 1) * 80;
        } else {
            vga[pos++] = (color << 8) | msg[i];
        }
        if (pos >= 80 * 25) pos = 0;
    }
}

// Shell entry point - runs in Ring 3
void shell_ring3_entry(void)
{
    // Clear screen
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    for (int i = 0; i < 80 * 25; i++) {
        vga[i] = 0x0F20; // White on black, space
    }
    
    // Show banner
    shell_write_vga("╔═══════════════════════════════════════════════════════╗\n", 0x0B);
    shell_write_vga("║         IR0 SHELL - Running in Ring 3 (User Mode)    ║\n", 0x0F);
    shell_write_vga("╚═══════════════════════════════════════════════════════╝\n", 0x0B);
    shell_write_vga("\n", 0x0F);
    shell_write_vga("Shell is now running in user space (Ring 3)\n", 0x0A);
    shell_write_vga("Interrupts are enabled. Keyboard should work!\n", 0x0A);
    shell_write_vga("Type something to test...\n", 0x07);
    shell_write_vga("\n", 0x0F);
    shell_write_vga("Shell> ", 0x0E);
    
    int cursor_pos = 7 * 80 + 7; // After "Shell> "
    int last_buffer_pos = 0;
    
    // Main loop - read keyboard and display
    for (;;) {
        // Check if there's new keyboard input
        int current_pos = *shell_keyboard_buffer_pos;
        
        while (last_buffer_pos < current_pos) {
            char c = shell_keyboard_buffer[last_buffer_pos++];
            
            if (c == '\n') {
                // Enter pressed - new line
                cursor_pos = (cursor_pos / 80 + 1) * 80;
                shell_write_vga("\nShell> ", 0x0E);
                cursor_pos = (cursor_pos / 80) * 80 + 7;
            } else if (c == '\b') {
                // Backspace
                if ((cursor_pos % 80) > 7) {
                    cursor_pos--;
                    shell_write_char(' ', cursor_pos, 0x0F);
                }
            } else if (c >= 32 && c < 127) {
                // Printable character
                shell_write_char(c, cursor_pos++, 0x0F);
                if (cursor_pos >= 80 * 25) cursor_pos = 0;
            }
        }
        
        // Blink cursor
        static int blink = 0;
        shell_write_char((blink++ & 0x10) ? '_' : ' ', cursor_pos, 0x0F);
        
        // Small delay
        for (volatile int i = 0; i < 100000; i++);
    }
}
