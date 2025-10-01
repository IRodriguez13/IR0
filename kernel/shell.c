// shell.c - Shell running in Ring 3 (user space)
#include <stdint.h>
#include <stddef.h>

// Syscall numbers
#define SYS_EXIT   0
#define SYS_WRITE  1
#define SYS_READ   2
#define SYS_GETPID 3

// Syscall wrapper - uses int 0x80
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

// Syscall wrappers
static inline int64_t sys_read(int fd, void *buf, size_t count)
{
    return syscall(SYS_READ, fd, (uint64_t)buf, count);
}

static inline int64_t sys_getpid(void)
{
    return syscall(SYS_GETPID, 0, 0, 0);
}

// VGA buffer and cursor management
static volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
static int cursor_pos = 0;

// Write directly to VGA buffer
static void vga_putchar(char c, uint8_t color)
{
    if (c == '\n') {
        cursor_pos = (cursor_pos / 80 + 1) * 80;
        if (cursor_pos >= 80 * 25) {
            // Scroll screen
            for (int i = 0; i < 24 * 80; i++) {
                vga[i] = vga[i + 80];
            }
            for (int i = 24 * 80; i < 25 * 80; i++) {
                vga[i] = 0x0F20;
            }
            cursor_pos = 24 * 80;
        }
    } else if (c == '\b') {
        if (cursor_pos > 0) {
            cursor_pos--;
            vga[cursor_pos] = (color << 8) | ' ';
        }
    } else {
        vga[cursor_pos] = (color << 8) | c;
        cursor_pos++;
        if (cursor_pos >= 80 * 25) {
            // Scroll screen
            for (int i = 0; i < 24 * 80; i++) {
                vga[i] = vga[i + 80];
            }
            for (int i = 24 * 80; i < 25 * 80; i++) {
                vga[i] = 0x0F20;
            }
            cursor_pos = 24 * 80;
        }
    }
}

// Write string to VGA
static void vga_print(const char *str, uint8_t color)
{
    for (int i = 0; str[i] != '\0'; i++) {
        vga_putchar(str[i], color);
    }
}

// Process commands
static void process_command(const char *cmd)
{
    if (cmd[0] == '\0') return;
    
    if (cmd[0] == 'l' && cmd[1] == 's' && (cmd[2] == '\0' || cmd[2] == ' ')) {
        vga_print("Executing ls...\n", 0x0A);
        syscall(5, (uint64_t)"/", 0, 0); // SYS_LS
    }
    else if (cmd[0] == 'p' && cmd[1] == 's' && cmd[2] == '\0') {
        vga_print("Executing ps...\n", 0x0A);
        syscall(7, 0, 0, 0); // SYS_PS
    }
    else if (cmd[0] == 'h' && cmd[1] == 'e' && cmd[2] == 'l' && cmd[3] == 'p') {
        vga_print("Available commands:\n", 0x0E);
        vga_print("  ls    - List files\n", 0x0F);
        vga_print("  ps    - Show processes\n", 0x0F);
        vga_print("  help  - Show this help\n", 0x0F);
        vga_print("  exit  - Exit shell\n", 0x0F);
    }
    else if (cmd[0] == 'e' && cmd[1] == 'x' && cmd[2] == 'i' && cmd[3] == 't') {
        vga_print("Exiting shell...\n", 0x0C);
        syscall(SYS_EXIT, 0, 0, 0);
    }
    else {
        vga_print("Unknown command: ", 0x0C);
        vga_print(cmd, 0x0F);
        vga_print("\nType 'help' for available commands\n", 0x0E);
    }
}

// Shell entry point - runs in Ring 3
void shell_ring3_entry(void)
{
    // Clear screen
    for (int i = 0; i < 80 * 25; i++) {
        vga[i] = 0x0F20; // White on black, space
    }
    cursor_pos = 0;
    
    // Show banner
    vga_print("=== IR0 SHELL ===\n", 0x0F);
    vga_print("Running in Ring 3\n", 0x0A);
    
    // Test getpid syscall
    int64_t pid = sys_getpid();
    vga_print("Process ID: ", 0x0E);
    if (pid >= 0 && pid <= 9) {
        char pid_str[2] = {'0' + (char)pid, '\0'};
        vga_print(pid_str, 0x0F);
    } else {
        vga_print("ERROR", 0x0C);
    }
    vga_print("\n", 0x0F);
    
    vga_print("Type 'help' for commands, ESC to exit\n", 0x0B);
    
    char buffer[64];
    int pos = 0;
    int echo_pos = 0; // Position for visual echo
    
    vga_print("shell> ", 0x0E);
    echo_pos = cursor_pos; // Remember where input starts
    
    // Main input loop
    while (1) {
        char c;
        int64_t bytes_read = sys_read(0, &c, 1); // STDIN
        
        if (bytes_read > 0) {
            if (c == '\n' || c == '\r') {
                // Enter - process command
                vga_putchar('\n', 0x0F);
                buffer[pos] = '\0';
                
                if (pos > 0) {
                    process_command(buffer);
                }
                
                // Reset for next command
                pos = 0;
                vga_print("shell> ", 0x0E);
                echo_pos = cursor_pos;
            }
            else if (c == '\b' || c == 127) {
                // Backspace - manejar visualmente
                if (pos > 0) {
                    pos--;
                    buffer[pos] = '\0';
                    
                    // Borrar visualmente: mover cursor atrás y escribir espacio
                    if (cursor_pos > echo_pos) {
                        cursor_pos--;
                        vga[cursor_pos] = 0x0F20; // Espacio blanco
                    }
                }
            }
            else if (c == 27) {
                // ESC - exit
                vga_print("\nExiting...\n", 0x0C);
                syscall(SYS_EXIT, 0, 0, 0);
            }
            else if (c >= 32 && c < 127 && pos < 63) {
                // Regular character
                buffer[pos++] = c;
                vga_putchar(c, 0x0F);
            }
        }
        else {
            // No input, small delay
            for (volatile int i = 0; i < 5000; i++);
        }
    }
}