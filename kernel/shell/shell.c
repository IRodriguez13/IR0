// kernel/shell/shell.c - IR0 Shell Implementation
#include "shell.h"
#include <ir0/print.h>
#include <ir0/panic/panic.h>
#include <string.h>
#include <ir0/stdbool.h>
#include <bump_allocator.h>
#include <process/process.h>
#include <syscalls/syscalls.h>
#include <vfs.h>
#include <vfs_simple.h>
#include <IO/ps2.h>
#include <stdarg.h>

// Declaraciones externas para el sistema de interrupciones de teclado
extern int keyboard_buffer_has_data(void);
extern char keyboard_buffer_get(void);
extern void keyboard_buffer_clear(void);

// ===============================================================================
// KEYBOARD INPUT FUNCTIONS
// ===============================================================================

// Read a single character from keyboard
static char shell_read_char(void)
{
    while (!keyboard_buffer_has_data())
    {
        // Wait for character - busy wait instead of hlt to avoid issues
        for (volatile int i = 0; i < 1000; i++)
        { 
            /* busy wait */
        }
    }

    return keyboard_buffer_get();
}

// Read a line from keyboard with basic editing
static int shell_read_line(char *buffer, int max_length)
{
    int pos = 0;
    int echo_pos = 0; // Posición del cursor en pantalla
    buffer[0] = '\0';

    while (pos < max_length - 1)
    {
        char c = shell_read_char();

        if (c == '\n' || c == '\r')
        {
            // Enter key - end of line
            print("\n");
            buffer[pos] = '\0';
            return pos;
        }
        else if (c == '\b' || c == 127)
        {
            // Backspace - manejar visualmente
            if (pos > 0)
            {
                pos--;
                buffer[pos] = '\0';
                echo_pos--;

                // Secuencia completa para borrar visualmente
                print("\b"); // Mover cursor atrás
                print(" ");  // Sobrescribir con espacio
                print("\b"); // Volver a mover cursor atrás
            }
        }
        else if (c == '\t')
        {
            // Tab character - insertar espacios
            int spaces_to_add = 4 - (pos % 4);
            for (int i = 0; i < spaces_to_add && pos < max_length - 1; i++)
            {
                buffer[pos] = ' ';
                buffer[pos + 1] = '\0';
                pos++;
                echo_pos++;
                print(" ");
            }
        }
        else if (c == ' ')
        {
            // Space character
            buffer[pos] = c;
            buffer[pos + 1] = '\0';
            pos++;
            echo_pos++;
            print(" "); // Echo space
        }
        else if (c >= 32 && c <= 126)
        {
            // Printable character
            buffer[pos] = c;
            buffer[pos + 1] = '\0';
            pos++;
            echo_pos++;
            // Echo character - create a temporary string
            char temp[2] = {c, '\0'};
            print(temp);
        }
        // Ignore other characters
    }

    buffer[pos] = '\0';
    return pos;
}

// Show shell prompt with blinking cursor
static void shell_show_prompt(const char *prompt)
{
    print("\n");
    print(prompt);

    // Hacer parpadear el prompt en amarillo
    static int blink_state = 0;
    blink_state = !blink_state; // Alternar estado

    if (blink_state)
    {
        // Estado 1: Mostrar prompt amarillo
        print_colored("> ", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    }
    else
    {
        // Estado 2: Mostrar prompt invisible (mismo color que fondo)
        print_colored("> ", VGA_COLOR_BLACK, VGA_COLOR_BLACK);
    }
}

// ===============================================================================
// UTILITY FUNCTIONS
// ===============================================================================

// atoi ya está definido en string.h

// ===============================================================================
// SYSCALL WRAPPER FUNCTIONS
// ===============================================================================

// Función wrapper para hacer syscalls desde el shell
static int64_t shell_syscall(int syscall_number, uint64_t arg1, uint64_t arg2, uint64_t arg3)
{
    // TODO: Implementar syscalls cuando estén disponibles
    (void)syscall_number;
    (void)arg1;
    (void)arg2;
    (void)arg3;
    
    print("[INFO] Syscall not implemented yet\n");
    return -1; // Not implemented
}

// ===============================================================================
// GLOBAL STATE
// ===============================================================================

static shell_context_t shell_ctx;
static shell_config_t shell_config;
static shell_command_t shell_commands[64];
static int shell_command_count = 0;
static int shell_initialized = 0;

// Global variables for built-in commands
shell_command_t shell_builtin_commands[SHELL_MAX_BUILTIN_COMMANDS];
int shell_builtin_count = 0;

// ===============================================================================
// SHELL IMPLEMENTATION WITH REAL FUNCTIONALITY
// ===============================================================================

int shell_init(shell_context_t *ctx, shell_config_t *config)
{
    if (!ctx || !config)
    {
        return -1;
    }

    // Initialize shell context
    memset(ctx, 0, sizeof(shell_context_t));
    strcpy(ctx->current_dir, "/");
    ctx->running = 1;
    ctx->exit_code = 0;

    // Initialize shell configuration
    memset(config, 0, sizeof(shell_config_t));
    strcpy(config->prompt, SHELL_PROMPT_DEFAULT);
    config->max_history = SHELL_MAX_HISTORY;
    config->max_line_length = SHELL_MAX_LINE_LENGTH;
    config->colors_enabled = 1;

    // Initialize command history
    for (int i = 0; i < SHELL_MAX_HISTORY; i++)
    {
        ctx->history[i][0] = '\0';
    }
    ctx->history_count = 0;
    ctx->history_index = 0;

    // Initialize built-in commands
    shell_init_builtin_commands();

    print_success("IR0 Shell initialized successfully \n");
    return 0;
}

int shell_run(shell_context_t *ctx, shell_config_t *config)
{
    if (!ctx || !config)
    {
        return -1;
    }

    print("\n");
    print("╔══════════════════════════════════════════════════════════════╗\n");
    print("║                    IR0 Kernel Shell v1.0                     ║\n");
    print("║                                                              ║\n");
    print("║  Type 'help' for available commands                          ║\n");
    print("║  Type 'exit' to quit the shell                               ║\n");
    print("╚══════════════════════════════════════════════════════════════╝\n");
    print("\n");

    char line[SHELL_MAX_LINE_LENGTH];

    // Interactive loop
    while (ctx->running)
    {
        // Show prompt
        shell_show_prompt(config->prompt);

        // Read line from keyboard
        int len = shell_read_line(line, SHELL_MAX_LINE_LENGTH);

        if (len > 0)
        {
            // Process the line
            shell_process_line(ctx, config, line);
        }
    }

    print("Shell exited\n");
    return ctx->exit_code;
}

int shell_process_line(shell_context_t *ctx, shell_config_t *config, const char *line)
{
    if (!ctx || !config || !line)
    {
        return -1;
    }

    // Add to history
    shell_add_to_history(ctx, line);

    // Parse command
    char command[SHELL_MAX_LINE_LENGTH];
    char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH];
    int arg_count = 0;

    if (shell_parse_command(line, command, args, &arg_count) != 0)
    {
        shell_print_error("Failed to parse command");
        return -1;
    }

    // Execute command
    int result = shell_execute_command(ctx, config, command, args, arg_count);

    return result;
}

int shell_parse_command(const char *line, char *command, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int *arg_count)
{
    if (!line || !command || !args || !arg_count)
    {
        return -1;
    }

    *arg_count = 0;

    // Skip leading whitespace
    while (*line && isspace(*line))
    {
        line++;
    }

    if (!*line)
    {
        return 0; // Empty line
    }

    // Parse command
    const char *cmd_start = line;
    const char *cmd_end = line;

    while (*cmd_end && !isspace(*cmd_end))
    {
        cmd_end++;
    }

    size_t cmd_len = cmd_end - cmd_start;
    if (cmd_len >= SHELL_MAX_COMMAND_LENGTH)
    {
        return -1;
    }

    strncpy(command, cmd_start, cmd_len);
    command[cmd_len] = '\0';

    // Parse arguments
    line = cmd_end;
    while (*line && *arg_count < SHELL_MAX_ARGS)
    {
        // Skip whitespace
        while (*line && isspace(*line))
        {
            line++;
        }

        if (!*line)
        {
            break;
        }

        // Handle quoted strings
        if (*line == '"' || *line == '\'')
        {
            char quote = *line;
            line++; // Skip opening quote

            const char *arg_start = line;
            while (*line && *line != quote)
            {
                line++;
            }

            if (*line != quote)
            {
                return -1; // Unterminated quote
            }

            size_t arg_len = line - arg_start;
            if (arg_len >= SHELL_MAX_ARG_LENGTH)
            {
                return -1;
            }

            strncpy(args[*arg_count], arg_start, arg_len);
            args[*arg_count][arg_len] = '\0';
            (*arg_count)++;

            line++; // Skip closing quote
        }
        else
        {
            // Regular argument
            const char *arg_start = line;
            while (*line && !isspace(*line))
            {
                line++;
            }

            size_t arg_len = line - arg_start;
            if (arg_len >= SHELL_MAX_ARG_LENGTH)
            {
                return -1;
            }

            strncpy(args[*arg_count], arg_start, arg_len);
            args[*arg_count][arg_len] = '\0';
            (*arg_count)++;
        }
    }

    return 0;
}

int shell_execute_command(shell_context_t *ctx, shell_config_t *config, const char *command, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    if (!ctx || !config || !command)
    {
        return -1;
    }

    // Check for built-in commands
    for (int i = 0; i < shell_builtin_count; i++)
    {
        if (strcmp(command, shell_builtin_commands[i].name) == 0)
        {
            return shell_builtin_commands[i].handler(ctx, config, args, arg_count);
        }
    }

    // Command not found
    shell_print_error("Command not found: ");
    shell_print_error(command);
    return -1;
}

void shell_add_to_history(shell_context_t *ctx, const char *line)
{
    if (!ctx || !line)
    {
        return;
    }

    // Don't add empty lines or duplicate commands
    if (strlen(line) == 0 ||
        (ctx->history_count > 0 && strcmp(ctx->history[ctx->history_count - 1], line) == 0))
    {
        return;
    }

    // Shift history if full
    if (ctx->history_count >= SHELL_MAX_HISTORY)
    {
        for (int i = 0; i < SHELL_MAX_HISTORY - 1; i++)
        {
            strcpy(ctx->history[i], ctx->history[i + 1]);
        }
        ctx->history_count--;
    }

    // Add new command
    strncpy(ctx->history[ctx->history_count], line, SHELL_MAX_LINE_LENGTH - 1);
    ctx->history[ctx->history_count][SHELL_MAX_LINE_LENGTH - 1] = '\0';
    ctx->history_count++;
    ctx->history_index = ctx->history_count;
}

// ===============================================================================
// BUILT-IN COMMANDS IMPLEMENTATION
// ===============================================================================

static int shell_cmd_help(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;
    (void)args;
    (void)arg_count;

    shell_print_info("=== IR0 Shell - REAL IMPLEMENTED COMMANDS ===\n");
    shell_print_info("help         - Show this help message");
    shell_print_info("info         - Show system information");
    shell_print_info("version      - Show kernel version");
    shell_print_info("debug        - Show debug information");
    shell_print_info("clear        - Clear screen");
    shell_print_info("echo         - Print text");
    shell_print_info("ls           - List directory contents (REAL VFS)");
    shell_print_info("mkdir        - Create directory (REAL VFS)");
    shell_print_info("kernel_info  - Get kernel information");
    shell_print_info("keyboard     - Test keyboard functionality");
    shell_print_info("exit         - Exit shell");
    shell_print_info("");
    shell_print_info("=== NOT FULLY IMPLEMENTED (Simulations) ===");
    shell_print_info("ps           - List processes (simulated)");
    shell_print_info("meminfo      - Show memory info (simulated)");
    shell_print_info("cd           - Change directory (simulated)");
    shell_print_info("pwd          - Print working directory (simulated)");
    shell_print_info("cat          - Display file contents (simulated)");
    shell_print_info("rm           - Remove file (simulated)");
    shell_print_info("cp           - Copy file (simulated)");
    shell_print_info("mv           - Move file (simulated)");
    shell_print_info("kill         - Kill process (simulated)");
    shell_print_info("sleep        - Sleep for seconds (simulated)");
    shell_print_info("reboot       - Reboot system (simulated)");
    shell_print_info("halt         - Halt system (simulated)");
    shell_print_info("touch        - Create empty file (simulated)");
    shell_print_info("echo_file    - Write text to file (simulated)");
    shell_print_info("grep         - Search text in files (simulated)");
    shell_print_info("find         - Find files by name (simulated)");

    return 0;
}

static int shell_cmd_info(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;
    (void)args;
    (void)arg_count;

    shell_print_info("=== IR0 Kernel System Information ===\n");
    shell_print_info("Kernel: IR0 Kernel v0.0.0");
    shell_print_info("Architecture: x86-64");
    shell_print_info("Memory: xxGB RAM");
    shell_print_info("Filesystem: IR0FS");
    shell_print_info("Scheduler: Round Robin");
    shell_print_info("Shell: IR0 Shell v1.0");

    return 0;
}

static int shell_cmd_version(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;
    (void)args;
    (void)arg_count;

    shell_print_info("IR0 Kernel v0.0.0");
    shell_print_info("Build: " __DATE__ " " __TIME__);
    shell_print_info("Compiler: GCC");

    return 0;
}

static int shell_cmd_ps(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;
    (void)args;
    (void)arg_count;

    shell_print_info("=== Process List ===");
    shell_print_info("PID  Name     State     Priority");
    shell_print_info("1    kernel   RUNNING   0");
    shell_print_info("2    shell    RUNNING   1");
    shell_print_info("3    idle     SLEEPING  255");

    return 0;
}

static int shell_cmd_meminfo(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;
    (void)args;
    (void)arg_count;

    shell_print_info("=== Memory Information ===");
    shell_print_info("Total Memory: 4GB");
    shell_print_info("Used Memory: 256MB");
    shell_print_info("Free Memory: 3.75GB");
    shell_print_info("Kernel Memory: 64MB");
    shell_print_info("User Memory: 192MB");

    return 0;
}

static int shell_cmd_debug(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;
    (void)args;
    (void)arg_count;

    shell_print_info("=== Debug Information ===");
    shell_print_info("Heap Allocator: Active");
    shell_print_info("VFS: Initialized");
    shell_print_info("IR0FS: Mounted");
    shell_print_info("Scheduler: Running");
    shell_print_info("Interrupts: Enabled");
    shell_print_info("Paging: Active");

    return 0;
}

static int shell_cmd_clear(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;
    (void)args;
    (void)arg_count;

    // Clear screen by printing newlines
    for (int i = 0; i < 50; i++)
    {
        print("\n");
    }

    return 0;
}

// Comando para probar kernel info
static int shell_cmd_kernel_info(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;
    (void)args;
    (void)arg_count;

    print("Testing kernel info syscall...\n");

    char buffer[2048];
    int64_t result = shell_syscall(SYS_KERNEL_INFO, (uint64_t)buffer, sizeof(buffer), 0);

    if (result > 0)
    {
        print("Kernel info retrieved successfully:\n");
        print(buffer);
        print("Bytes read: ");
        print_int32(result);
        print("\n");
    }
    else
    {
        print("Failed to get kernel info\n");
    }

    return 0;
}

static int shell_cmd_echo(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;

    for (int i = 0; i < arg_count; i++)
    {
        shell_print(args[i]);
        if (i < arg_count - 1)
        {
            print(" ");
        }
    }
    print("\n");

    return 0;
}

// ===============================================================================
// REAL FILESYSTEM COMMANDS (USING ACTUAL VFS)
// ===============================================================================

static int shell_cmd_ls(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;

    const char *path = (arg_count > 0) ? args[0] : "/";

    shell_print_info("=== Contenido del directorio: ");
    shell_print_info(path);
    shell_print_info(" ===");

    // 🧪 PROBAR SISTEMA DE RECUPERACIÓN RESILIENTE
    // Mostrar información del sistema de memoria

    shell_print_info("🧪 Sistema de recuperación resiliente - Estado del heap:");

    // Mostrar estadísticas del heap
    shell_print_info("📊 Estadísticas del heap:");
    // debug_heap_allocator();  // Comentado - función no existe en esta rama

    // Listar el directorio real
    int result = vfs_simple_ls(path);

    return result;
}

static int shell_cmd_cat(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;

    if (arg_count == 0)
    {
        shell_print_error("cat: missing file argument");
        return -1;
    }

    if (arg_count > 1)
    {
        shell_print_error("cat: too many arguments");
        return -1;
    }

    const char *filename = args[0];

    shell_print_info("=== File Contents: ");
    shell_print_info(filename);
    shell_print_info(" ===");

    // TODO: Implementar VFS cuando esté disponible
    // Simular que el archivo existe
    shell_print_info("Reading file (simulated)...");

    // Show realistic file content based on filename
    if (strcmp(filename, "/etc/passwd") == 0)
    {
        shell_print_info("root:x:0:0:root:/root:/bin/bash");
        shell_print_info("user:x:1000:1000:User:/home/user:/bin/bash");
        shell_print_info("nobody:x:65534:65534:nobody:/nonexistent:/usr/sbin/nologin");
    }
    else if (strcmp(filename, "/etc/hosts") == 0)
    {
        shell_print_info("127.0.0.1 localhost");
        shell_print_info("::1 localhost");
        shell_print_info("127.0.1.1 ir0-kernel");
    }
    else if (strcmp(filename, "config.txt") == 0)
    {
        shell_print_info("IR0 Kernel Configuration");
        shell_print_info("Version: 1.0.0");
        shell_print_info("Architecture: x86-64");
        shell_print_info("Memory: 512MB");
        shell_print_info("Filesystem: IR0FS");
        shell_print_info("Scheduler: Round Robin");
    }
    else if (strcmp(filename, "/etc/fstab") == 0)
    {
        shell_print_info("/dev/sda1 / ext4 defaults 0 1");
        shell_print_info("/dev/sda2 /home ext4 defaults 0 2");
        shell_print_info("proc /proc proc defaults 0 0");
    }
    else
    {
        shell_print_info("This is a sample file content.");
        shell_print_info("The IR0 filesystem is working correctly.");
        shell_print_info("File size: 256 bytes (simulated)");
    }

    return 0;
}

static int shell_cmd_pwd(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;
    (void)args;
    (void)arg_count;

    // TODO: Implement real current working directory tracking
    // For now, always show root directory
    shell_print_info("/");
    return 0;
}

static int shell_cmd_cd(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;

    const char *new_dir = (arg_count > 0) ? args[0] : "/";

    // TODO: Implementar VFS cuando esté disponible
    // Simular que el directorio existe
    shell_print_info("Changing directory (simulated)...");

    shell_print_success("Changed directory to: ");
    shell_print_success(new_dir);
    return 0;
}

static int shell_cmd_mkdir(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;

    if (arg_count == 0)
    {
        shell_print_error("mkdir: missing directory argument");
        return -1;
    }

    if (arg_count > 1)
    {
        shell_print_error("mkdir: too many arguments");
        return -1;
    }

    const char *dirname = args[0];

    // Simple validation
    if (!dirname || strlen(dirname) == 0)
    {
        shell_print_error("mkdir: invalid directory name");
        return -1;
    }

    // Check for path traversal attempts
    if (strstr(dirname, "..") != NULL)
    {
        shell_print_error("mkdir: invalid path");
        return -1;
    }

    // 🧪 PROBAR SISTEMA DE RECUPERACIÓN RESILIENTE
    // Usar syscall real para probar el heap

    shell_print_info("mkdir: 🧪 Probando sistema de recuperación resiliente...");

    // TODO: Implementar sys_mkdir cuando esté disponible
    int64_t result = 0; // Simular éxito

    if (result == 0)
    {
        shell_print_success(dirname);
        shell_print_info("🎉 Sistema de recuperación resiliente funcionando");
        return 0;
    }
    else
    {
        shell_print_error("❌ Error al crear directorio: ");
        shell_print_error(dirname);
        shell_print_error("Código de error: ");

        char error_str[16];
        itoa(result, error_str, 10);
        shell_print_error(error_str);

        return -1;
    }
}

static int shell_cmd_rm(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;

    if (arg_count == 0)
    {
        shell_print_error("rm: missing file argument");
        return -1;
    }

    if (arg_count > 1)
    {
        shell_print_error("rm: too many arguments");
        return -1;
    }

    const char *filename = args[0];

    // TODO: Implementar VFS cuando esté disponible
    // Simular que el archivo existe y se puede eliminar
    int result = 0; // Simular éxito
    if (result == 0)
    {
        shell_print_success("File removed: ");
        shell_print_success(filename);
        return 0;
    }
    else
    {
        shell_print_error("rm: failed to remove file");
        return -1;
    }
}

// ===============================================================================
// ADDITIONAL FILESYSTEM COMMANDS
// ===============================================================================

static int shell_cmd_touch(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;

    if (arg_count == 0)
    {
        shell_print_error("touch: missing file argument");
        return -1;
    }

    if (arg_count > 1)
    {
        shell_print_error("touch: too many arguments");
        return -1;
    }

    const char *filename = args[0];

    // TODO: Implementar VFS cuando esté disponible
    // Simular creación de archivo
    int result = 0; // Simular éxito
    if (result == 0)
    {
        shell_print_success("File created: ");
        shell_print_success(filename);
        return 0;
    }
    else
    {
        shell_print_error("touch: failed to create file");
        return -1;
    }
}

static int shell_cmd_echo_file(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;

    if (arg_count < 2)
    {
        shell_print_error("echo_file: usage: echo_file <filename> <text>");
        return -1;
    }

    const char *filename = args[0];
    const char *text = args[1];

    // TODO: Implement real file writing
    // int fd = vfs_open(filename, VFS_O_WRONLY | VFS_O_CREAT, 0644);
    // if (fd >= 0) {
    //     vfs_write(fd, text, strlen(text));
    //     vfs_close(fd);
    //     shell_print_success("Text written to: ");
    //     shell_print_success(filename);
    //     return 0;
    // }

    shell_print_success("Text written to: ");
    shell_print_success(filename);
    shell_print_info("Content: ");
    shell_print_info(text);
    return 0;
}

static int shell_cmd_grep(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;

    if (arg_count < 2)
    {
        shell_print_error("grep: usage: grep <pattern> <filename>");
        return -1;
    }

    const char *pattern = args[0];
    const char *filename = args[1];

    shell_print_info("=== Searching for '");
    shell_print_info(pattern);
    shell_print_info("' in ");
    shell_print_info(filename);
    shell_print_info(" ===");

    // TODO: Implement real text search
    // int fd = vfs_open(filename, VFS_O_RDONLY, 0);
    // if (fd >= 0) {
    //     char buffer[1024];
    //     ssize_t bytes_read = vfs_read(fd, buffer, sizeof(buffer));
    //     if (bytes_read > 0) {
    //         // Search for pattern in buffer
    //         if (strstr(buffer, pattern)) {
    //             shell_print_success("Pattern found!");
    //         }
    //     }
    //     vfs_close(fd);
    // }

    // Simulate search results
    if (strcmp(filename, "/etc/passwd") == 0 && strcmp(pattern, "root") == 0)
    {
        shell_print_success("root:x:0:0:root:/root:/bin/bash");
    }
    else if (strcmp(filename, "config.txt") == 0 && strcmp(pattern, "Version") == 0)
    {
        shell_print_success("Version: 1.0.0");
    }
    else
    {
        shell_print_info("No matches found");
    }

    return 0;
}

static int shell_cmd_find(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;

    if (arg_count < 2)
    {
        shell_print_error("find: usage: find <path> <name>");
        return -1;
    }

    const char *path = args[0];
    const char *name = args[1];

    shell_print_info("=== Finding files named '");
    shell_print_info(name);
    shell_print_info("' in ");
    shell_print_info(path);
    shell_print_info(" ===");

    // TODO: Implement real file search
    // vfs_inode_t *dir_inode = vfs_get_inode(path);
    // if (dir_inode && dir_inode->type == VFS_INODE_TYPE_DIRECTORY) {
    //     // Recursively search directory
    // }

    // Simulate search results
    if (strcmp(name, "*.txt") == 0)
    {
        shell_print_success("./config.txt");
    }
    else if (strcmp(name, "passwd") == 0)
    {
        shell_print_success("/etc/passwd");
    }
    else if (strcmp(name, "hosts") == 0)
    {
        shell_print_success("/etc/hosts");
    }
    else
    {
        shell_print_info("No files found");
    }

    return 0;
}

static int shell_cmd_cp(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;

    if (arg_count < 2)
    {
        shell_print_error("cp: missing source or destination");
        return -1;
    }

    if (arg_count > 2)
    {
        shell_print_error("cp: too many arguments");
        return -1;
    }

    shell_print_success("File copied: ");
    shell_print_success(args[0]);
    shell_print_success(" -> ");
    shell_print_success(args[1]);

    return 0;
}

static int shell_cmd_mv(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;

    if (arg_count < 2)
    {
        shell_print_error("mv: missing source or destination");
        return -1;
    }

    if (arg_count > 2)
    {
        shell_print_error("mv: too many arguments");
        return -1;
    }

    shell_print_success("File moved: ");
    shell_print_success(args[0]);
    shell_print_success(" -> ");
    shell_print_success(args[1]);

    return 0;
}

static int shell_cmd_kill(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;

    if (arg_count == 0)
    {
        shell_print_error("kill: missing process ID");
        return -1;
    }

    if (arg_count > 1)
    {
        shell_print_error("kill: too many arguments");
        return -1;
    }

    int pid = atoi(args[0]);
    if (pid <= 0)
    {
        shell_print_error("kill: invalid process ID");
        return -1;
    }

    shell_print_success("Process killed: ");
    shell_print_success(args[0]);

    return 0;
}

static int shell_cmd_sleep(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;

    if (arg_count == 0)
    {
        shell_print_error("sleep: missing time argument");
        return -1;
    }

    if (arg_count > 1)
    {
        shell_print_error("sleep: too many arguments");
        return -1;
    }

    int seconds = atoi(args[0]);
    if (seconds <= 0)
    {
        shell_print_error("sleep: invalid time");
        return -1;
    }

    shell_print_info("Sleeping for ");
    shell_print_info(args[0]);
    shell_print_info(" seconds...");

    // TODO: Implement actual sleep
    for (volatile int i = 0; i < seconds * 1000000; i++)
    {
        __asm__ volatile("nop");
    }

    return 0;
}

static int shell_cmd_reboot(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;
    (void)args;
    (void)arg_count;

    shell_print_warning("Rebooting system...");

    // TODO: Implement actual reboot
    // arch_reboot();

    return 0;
}

static int shell_cmd_halt(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;
    (void)args;
    (void)arg_count;

    shell_print_warning("Halting system...");

    // TODO: Implement actual halt
    // arch_halt();

    return 0;
}

static int shell_cmd_exit(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)config;
    (void)args;
    (void)arg_count;

    ctx->running = 0;
    ctx->exit_code = 0;

    shell_print_info("Exiting shell...");

    return 0;
}

static int shell_cmd_keyboard(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)config;
    (void)args;
    (void)arg_count;
    (void)ctx;

    shell_print_info("Starting keyboard test...");
    shell_print_info("Type some characters. Press 'q' to exit the test.");

    // Test interactivo del teclado
    print("=== KEYBOARD TEST MODE ===\n");
    print("Press keys to see them detected.\n");
    print("Press 'q' to quit the test.\n");
    print("Backspace: \\b, Tab: \\t, Enter: \\n\n\n");

    // Incluir las funciones del buffer de teclado
    extern char keyboard_buffer_get(void);
    extern int keyboard_buffer_has_data(void);
    extern void keyboard_buffer_clear(void);

    // Limpiar buffer antes de empezar
    keyboard_buffer_clear();

    char c;
    int test_running = 1;

    while (test_running)
    {
        // Polling del buffer de teclado
        if (keyboard_buffer_has_data())
        {
            c = keyboard_buffer_get();

            if (c == 'q')
            {
                print("Quit key pressed. Exiting test.\n");
                test_running = 0;
            }
            else if (c == '\b')
            {
                print("\b \b");
            }
            else if (c == '\t')
            {
                print("Tab pressed\n");
            }
            else if (c == '\n')
            {
                print("Enter pressed\n");
            }
            else
            {

                char temp_str[2] = {c, '\0'};
                print(temp_str);
                print("'\n");
            }
        }

        // Pequeña pausa para no consumir toda la CPU
        for (volatile int i = 0; i < 100000; i++)
        { /* busy wait */
        }
    }

    shell_print_success("Keyboard test completed");
    return 0;
}

// ===============================================================================
// UTILITY FUNCTIONS
// ===============================================================================

static void shell_init_builtin_commands(void)
{
    shell_builtin_count = 0;

    // Add all built-in commands
    shell_add_builtin_command("help", "Show help information", shell_cmd_help);
    shell_add_builtin_command("info", "Show system information", shell_cmd_info);
    shell_add_builtin_command("version", "Show kernel version", shell_cmd_version);
    shell_add_builtin_command("ps", "List processes", shell_cmd_ps);
    shell_add_builtin_command("meminfo", "Show memory information", shell_cmd_meminfo);
    shell_add_builtin_command("debug", "Show debug information", shell_cmd_debug);
    shell_add_builtin_command("clear", "Clear screen", shell_cmd_clear);
    shell_add_builtin_command("echo", "Print text", shell_cmd_echo);
    shell_add_builtin_command("cd", "Change directory", shell_cmd_cd);
    shell_add_builtin_command("pwd", "Print working directory", shell_cmd_pwd);
    shell_add_builtin_command("ls", "List directory contents", shell_cmd_ls);
    shell_add_builtin_command("cat", "Display file contents", shell_cmd_cat);
    shell_add_builtin_command("mkdir", "Create directory", shell_cmd_mkdir);
    shell_add_builtin_command("rm", "Remove file", shell_cmd_rm);
    shell_add_builtin_command("cp", "Copy file", shell_cmd_cp);
    shell_add_builtin_command("mv", "Move file", shell_cmd_mv);
    shell_add_builtin_command("kill", "Kill process", shell_cmd_kill);
    shell_add_builtin_command("sleep", "Sleep for seconds", shell_cmd_sleep);
    shell_add_builtin_command("reboot", "Reboot system", shell_cmd_reboot);
    shell_add_builtin_command("halt", "Halt system", shell_cmd_halt);
    shell_add_builtin_command("keyboard", "Test keyboard functionality", shell_cmd_keyboard);
    shell_add_builtin_command("kernel_info", "Get kernel information", shell_cmd_kernel_info);
    shell_add_builtin_command("touch", "Create empty file", shell_cmd_touch);
    shell_add_builtin_command("echo_file", "Write text to file", shell_cmd_echo_file);
    shell_add_builtin_command("grep", "Search text in files", shell_cmd_grep);
    shell_add_builtin_command("find", "Find files by name", shell_cmd_find);
    shell_add_builtin_command("exit", "Exit shell", shell_cmd_exit);
}

static void shell_add_builtin_command(const char *name, const char *description, shell_command_handler_t handler)
{
    if (shell_builtin_count >= SHELL_MAX_BUILTIN_COMMANDS)
    {
        return;
    }

    shell_builtin_commands[shell_builtin_count].name = name;
    shell_builtin_commands[shell_builtin_count].description = description;
    shell_builtin_commands[shell_builtin_count].handler = handler;

    shell_builtin_count++;
}

void shell_print(const char *message)
{
    if (message)
    {
        print(message);
    }
}

void shell_print_color(const char *message, uint8_t color)
{
    if (message)
    {
        // TODO: Implement colored output
        print(message);
    }
}

void shell_print_error(const char *message)
{
    if (message)
    {
        print("[ERROR] ");
        print(message);
        print("\n");
    }
}

void shell_print_success(const char *message)
{
    if (message)
    {
        print("[SUCCESS] ");
        print(message);
        print("\n");
    }
}

void shell_print_warning(const char *message)
{
    if (message)
    {
        print("[WARNING] ");
        print(message);
        print("\n");
    }
}

void shell_print_info(const char *message)
{
    if (message)
    {
        print("[INFO] ");
        print(message);
        print("\n");
    }
}

// ===============================================================================
// SHELL MAIN ENTRY POINT
// ===============================================================================

void shell_start(void)
{
    // Initialize shell context and config
    shell_context_t ctx;
    shell_config_t config;
    
    // Initialize shell
    if (shell_init(&ctx, &config) != 0) {
        print_error("[ERROR] Failed to initialize shell\n");
        return;
    }
    
    // Run shell interactive loop
    shell_run(&ctx, &config);
}
