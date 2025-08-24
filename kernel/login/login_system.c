#include "login_system.h"
#include <ir0/print.h>
#include <ir0/panic/panic.h>
#include <drivers/IO/ps2.h>
#include <string.h>
#include <arch/common/arch_interface.h>

// Global login state
static login_state_t login_state = {0};
static login_config_t login_config = {0};

// Function to initialize login system
int login_init(const login_config_t *config) {
    if (!config || !config->correct_password) {
        return -1;
    }
    
    login_config = *config;
    login_state.attempts = 0;
    login_state.authenticated = false;
    login_state.locked = false;
    
    return 0;
}

// Function to authenticate user
int login_authenticate(void) {
    if (login_state.locked) {
        return -1; // System is locked
    }
    
    const char *correct_password = login_config.correct_password;
    const int max_attempts = login_config.max_attempts;
    char input_buffer[256];
    
    print_colored("╔══════════════════════════════════════════════════════════════════════════════╗\n", VGA_COLOR_BLUE, VGA_COLOR_BLACK);
    print_colored("║                              IR0 KERNEL LOGIN                               ║\n", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    print_colored("╚══════════════════════════════════════════════════════════════════════════════╝\n", VGA_COLOR_BLUE, VGA_COLOR_BLACK);
    
    while (login_state.attempts < max_attempts) {
        int remaining_attempts = max_attempts - login_state.attempts;
        
        print_colored("\n[LOGIN] Enter password (", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
        print_colored("admin", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        print_colored("): ", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
        
        // Clear input buffer
        memset(input_buffer, 0, sizeof(input_buffer));
        
        // Simple input reading
        size_t i = 0;
        while (i < sizeof(input_buffer) - 1) {
            // Wait for key press
            while (!(inb(0x64) & 0x01));
            
            uint8_t scancode = inb(0x60);
            
            // Handle key press
            if (scancode == 0x1C) { // Enter key
                input_buffer[i] = '\0';
                break;
            } else if (scancode == 0x0E) { // Backspace
                if (i > 0) {
                    i--;
                    print_colored("\b \b", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                }
            } else if (scancode < 0x80) { // Regular key press
                char key = ps2_scancode_to_ascii(scancode);
                if (key != 0) {
                    input_buffer[i] = key;
                    print_colored("*", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                    i++;
                }
            }
        }
        
        print_colored("\n", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        
        // Check password
        if (strcmp(input_buffer, correct_password) == 0) {
            login_state.authenticated = true;
            print_colored("[SUCCESS] Password correct! Welcome to IR0 Kernel\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
            return 0;
        } else {
            login_state.attempts++;
            remaining_attempts = max_attempts - login_state.attempts;
            
            print_colored("[ERROR] Invalid password. ", VGA_COLOR_RED, VGA_COLOR_BLACK);
            print_colored("Attempts remaining: ", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
            print_uint32(remaining_attempts);
            print_colored("\n", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            
            if (remaining_attempts > 0) {
                print_colored("[INFO] Please try again...\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
                delay_ms(2000);
            }
        }
    }
    
    // Too many failed attempts
    login_state.locked = true;
    print_colored("╔══════════════════════════════════════════════════════════════════════════════╗\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
    print_colored("║                           ACCESS DENIED                                     ║\n", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    print_colored("║                    Too many failed login attempts                          ║\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    print_colored("╚══════════════════════════════════════════════════════════════════════════════╝\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
    
    print_colored("[SYSTEM] Halting system due to security violation...\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
    delay_ms(3000);
    
    // Halt the system
    __asm__ volatile("cli");
    __asm__ volatile("hlt");
    
    return -1;
}

// Function to reset login state
void login_reset(void) {
    login_state.attempts = 0;
    login_state.authenticated = false;
    login_state.locked = false;
}

// Function to check if user is authenticated
bool login_is_authenticated(void) {
    return login_state.authenticated;
}

// Function to lock the system
void login_lock_system(void) {
    login_state.locked = true;
}
