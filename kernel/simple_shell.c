// simple_shell.c - Versión ultra-simplificada para debugging
#include <stdint.h>

// Syscall numbers
#define SYS_EXIT   0
#define SYS_WRITE  1
#define SYS_READ   2
#define SYS_GETPID 3

// Simple syscall wrapper
static inline int64_t syscall(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3)
{
    int64_t ret;
    __asm__ volatile(
        "mov %1, %%rax\n"
        "mov %2, %%rbx\n"
        "mov %3, %%rcx\n"
        "mov %4, %%rdx\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(num), "r"(arg1), "r"(arg2), "r"(arg3)
        : "rax", "rbx", "rcx", "rdx", "memory"
    );
    return ret;
}

// Write directly to VGA
static int cursor_pos = 0;

static void write_vga(const char *msg, int color)
{
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    
    for (int i = 0; msg[i] != '\0'; i++) {
        if (msg[i] == '\n') {
            cursor_pos = (cursor_pos / 80 + 1) * 80;
            if (cursor_pos >= 80 * 25) {
                // Scroll screen
                for (int j = 0; j < 24 * 80; j++) {
                    vga[j] = vga[j + 80];
                }
                for (int j = 24 * 80; j < 25 * 80; j++) {
                    vga[j] = 0x0F20;
                }
                cursor_pos = 24 * 80;
            }
        } else if (msg[i] == '\b') {
            if (cursor_pos > 0) {
                cursor_pos--;
                vga[cursor_pos] = (color << 8) | ' ';
            }
        } else {
            vga[cursor_pos] = (color << 8) | msg[i];
            cursor_pos++;
            if (cursor_pos >= 80 * 25) {
                // Scroll screen
                for (int j = 0; j < 24 * 80; j++) {
                    vga[j] = vga[j + 80];
                }
                for (int j = 24 * 80; j < 25 * 80; j++) {
                    vga[j] = 0x0F20;
                }
                cursor_pos = 24 * 80;
            }
        }
    }
}

// Convert number to hex string
static void write_hex(uint8_t value, int color)
{
    char hex[3];
    hex[0] = "0123456789ABCDEF"[(value >> 4) & 0xF];
    hex[1] = "0123456789ABCDEF"[value & 0xF];
    hex[2] = '\0';
    write_vga(hex, color);
}

// Simple command processing
static void process_command(const char *cmd)
{
    if (cmd[0] == '\0') return; // Empty command
    
    // Compare commands
    if (cmd[0] == 'l' && cmd[1] == 's' && (cmd[2] == '\0' || cmd[2] == ' ')) {
        write_vga("Executing ls...\n", 0x0A);
        syscall(5, (uint64_t)"/", 0, 0); // SYS_LS
    }
    else if (cmd[0] == 'p' && cmd[1] == 's' && cmd[2] == '\0') {
        write_vga("Executing ps...\n", 0x0A);
        syscall(7, 0, 0, 0); // SYS_PS
    }
    else if (cmd[0] == 'h' && cmd[1] == 'e' && cmd[2] == 'l' && cmd[3] == 'p') {
        write_vga("Available commands:\n", 0x0E);
        write_vga("  ls    - List files\n", 0x0F);
        write_vga("  ps    - Show processes\n", 0x0F);
        write_vga("  help  - Show this help\n", 0x0F);
        write_vga("  exit  - Exit shell\n", 0x0F);
    }
    else if (cmd[0] == 'e' && cmd[1] == 'x' && cmd[2] == 'i' && cmd[3] == 't') {
        write_vga("Exiting shell...\n", 0x0C);
        syscall(SYS_EXIT, 0, 0, 0);
    }
    else {
        write_vga("Unknown command: ", 0x0C);
        write_vga(cmd, 0x0F);
        write_vga("\nType 'help' for available commands\n", 0x0E);
    }
}

// Simple shell entry point
void simple_shell_ring3_entry(void)
{
    // Clear screen
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    for (int i = 0; i < 80 * 25; i++) {
        vga[i] = 0x0F20;
    }
    
    write_vga("=== IR0 SIMPLE SHELL ===\n", 0x0F);
    write_vga("Running in Ring 3\n", 0x0A);
    
    // Test getpid syscall
    int64_t pid = syscall(SYS_GETPID, 0, 0, 0);
    write_vga("Process ID: ", 0x0E);
    write_hex((uint8_t)pid, 0x0F);
    write_vga("\n", 0x0F);
    
    write_vga("Type 'help' for commands, ESC to exit\n", 0x0B);
    
    char input_buffer[64];
    int input_pos = 0;
    
    write_vga("shell> ", 0x0E);
    
    // Main input loop
    while (1) {
        char c;
        int64_t bytes_read = syscall(SYS_READ, 0, (uint64_t)&c, 1);
        
        if (bytes_read > 0) {
            if (c == '\n' || c == '\r') {
                // Process command
                write_vga("\n", 0x0F);
                input_buffer[input_pos] = '\0';
                
                if (input_pos > 0) {
                    process_command(input_buffer);
                }
                
                // Reset for next command
                input_pos = 0;
                write_vga("shell> ", 0x0E);
            }
            else if (c == '\b' || c == 127) {
                // Backspace
                if (input_pos > 0) {
                    input_pos--;
                    cursor_pos--;
                    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
                    vga[cursor_pos] = 0x0F20; // Clear character
                }
            }
            else if (c == 27) {
                // ESC - exit
                write_vga("\nExiting...\n", 0x0C);
                syscall(SYS_EXIT, 0, 0, 0);
            }
            else if (c >= 32 && c < 127 && input_pos < 63) {
                // Regular character
                input_buffer[input_pos++] = c;
                char str[2] = {c, '\0'};
                write_vga(str, 0x0F);
            }
        }
        else {
            // No input, small delay
            for (volatile int i = 0; i < 5000; i++);
        }
    }
}