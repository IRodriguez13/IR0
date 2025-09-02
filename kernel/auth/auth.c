#include "auth.h"
#include <ir0/print.h>
#include <string.h>
#include <stddef.h>

// ===============================================================================
// AUTHENTICATION SYSTEM IMPLEMENTATION
// ===============================================================================

// External keyboard functions
extern int keyboard_buffer_has_data(void);
extern char keyboard_buffer_get(void);

// Global authentication state
static auth_config_t auth_config;
static user_credentials_t current_user;
static bool is_authenticated = false;
static bool system_locked = false;
static int failed_attempts = 0;

// Default users database (simple for now)
static user_credentials_t users[] = 
{
    {"admin", "", 0, 0, 0xFFFFFFFF},
    {"root", "", 0, 0, 0xFFFFFFFF},
    {"user", "", 1000, 1000, 0x00000001}
};
static const int num_users = sizeof(users) / sizeof(users[0]);

// ===============================================================================
// PRIVATE FUNCTIONS
// ===============================================================================

static void auth_display_banner(void)
{
    print_colored("\n╔══════════════════════════════════════════════════════════════╗\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("║                    IR0 KERNEL LOGIN SYSTEM                   ║\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("║                                                              ║\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("║                  Secure Access Required                     ║\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("║                                                              ║\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("╚══════════════════════════════════════════════════════════════╝\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("\n", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

static int auth_read_input(char *buffer, int max_length, bool echo)
{
    int pos = 0;
    buffer[0] = '\0';
    
    while (pos < max_length - 1) 
    {
        // Wait for keyboard input
        while (!keyboard_buffer_has_data()) 
        {
            for (volatile int i = 0; i < 1000; i++) { /* busy wait */ }
        }
        
        char c = keyboard_buffer_get();
        
        if (c == '\n' || c == '\r') 
        {
            // Enter pressed
            buffer[pos] = '\0';
            print("\n");
            break;
        }
        else if (c == '\b' || c == 127) 
        {
            // Backspace
            if (pos > 0) 
            {
                pos--;
                buffer[pos] = '\0';
                if (echo) 
                {
                    print("\b \b");
                } 
                else 
                {
                    print("\b \b");
                }
            }
        }
        else if (c >= 32 && c <= 126) 
        {
            // Valid character
            buffer[pos] = c;
            buffer[pos + 1] = '\0';
            pos++;
            
            // Echo character (or asterisk for passwords)
            if (echo) 
            {
                char temp[2] = {c, '\0'};
                print(temp);
            } 
            else 
            {
                print("*");
            }
        }
    }
    
    return pos;
}

static auth_result_t auth_validate_user(const char *username)
{
    for (int i = 0; i < num_users; i++) 
    {
        if (strcmp(username, users[i].username) == 0) 
        {
            // Copy user data
            memcpy(&current_user, &users[i], sizeof(user_credentials_t));
            is_authenticated = true;
            failed_attempts = 0;
            return AUTH_SUCCESS;
        }
    }
    
    failed_attempts++;
    return AUTH_INVALID_CREDENTIALS;
}

static void auth_handle_lockout(void)
{
    system_locked = true;
    print_colored("🔒 SYSTEM LOCKED: Too many failed authentication attempts\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
    print_colored("💀 Access denied. System halting for security...\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
    
    // Halt the system
    for (;;) 
    {
        __asm__ volatile("hlt");
    }
}

// ===============================================================================
// PUBLIC FUNCTIONS
// ===============================================================================

int auth_init(auth_config_t *config)
{
    if (config) 
    {
        memcpy(&auth_config, config, sizeof(auth_config_t));
    } 
    else 
    {
        // Default configuration
        auth_config.max_attempts = 3;
        auth_config.lockout_time = 0;
        auth_config.require_password = false;
        auth_config.case_sensitive = true;
    }
    
    is_authenticated = false;
    system_locked = false;
    failed_attempts = 0;
    
    return 0;
}

auth_result_t kernel_login(void)
{
    char username[64];
    
    if (system_locked) 
    {
        return AUTH_SYSTEM_LOCKED;
    }
    
    auth_display_banner();
    
    while (failed_attempts < auth_config.max_attempts) 
    {
        print_colored("IR0-Kernel login: ", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        
        // Read username
        int len = auth_read_input(username, sizeof(username), true);
        
        if (len == 0) 
        {
            print_colored("❌ Username cannot be empty\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
            continue;
        }
        
        // Validate user
        auth_result_t result = auth_validate_user(username);
        
        if (result == AUTH_SUCCESS) 
        {
            print_colored("✅ Authentication successful! Welcome, ", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
            print_colored(current_user.username, VGA_COLOR_GREEN, VGA_COLOR_BLACK);
            print_colored(".\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
            print_colored("🔓 Access granted to IR0 Kernel.\n\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
            return AUTH_SUCCESS;
        }
        else 
        {
            print_colored("❌ Authentication failed! Invalid username.\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
            
            int remaining = auth_config.max_attempts - failed_attempts;
            if (remaining > 0) 
            {
                print_colored("Attempts remaining: ", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
                print_uint32(remaining);
                print_colored("\n\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
            }
        }
    }
    
    // Too many failed attempts
    auth_handle_lockout();
    return AUTH_TOO_MANY_ATTEMPTS; // Never reached
}

auth_result_t auth_user_simple(const char *username)
{
    if (!username) 
    {
        return AUTH_INVALID_CREDENTIALS;
    }
    
    if (system_locked) 
    {
        return AUTH_SYSTEM_LOCKED;
    }
    
    return auth_validate_user(username);
}

auth_result_t auth_user_full(const char *username, const char *password)
{
    // For future implementation with passwords
    (void)password;
    return auth_user_simple(username);
}

user_credentials_t *auth_get_current_user(void)
{
    return is_authenticated ? &current_user : NULL;
}

void auth_logout(void)
{
    is_authenticated = false;
    memset(&current_user, 0, sizeof(user_credentials_t));
}

bool auth_is_system_locked(void)
{
    return system_locked;
}

void auth_reset_attempts(void)
{
    failed_attempts = 0;
    system_locked = false;
}
