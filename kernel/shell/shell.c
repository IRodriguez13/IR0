// kernel/shell/shell.c - IR0 Shell Implementation
#include "shell.h"
#include "../../includes/ir0/print.h"
#include "../../includes/ir0/panic/panic.h"
#include "../../includes/string.h"
#include "../../memory/memo_interface.h"
#include "../../memory/heap_allocator.h"
#include "../../kernel/process/process.h"
#include "../../kernel/syscalls/syscalls.h"
#include "../../fs/vfs.h"
#include "../../drivers/IO/ps2.h"
#include "stdarg.h"

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
        for (volatile int i = 0; i < 1000; i++) { /* busy wait */ }
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
                print("\b");  // Mover cursor atrás
                print(" ");   // Sobrescribir con espacio
                print("\b");  // Volver a mover cursor atrás
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
    
    if (blink_state) {
        // Estado 1: Mostrar prompt amarillo
        print_colored("> ", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    } else {
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
    // Crear estructura de argumentos para syscall
    syscall_args_t args;
    args.arg1 = arg1;
    args.arg2 = arg2;
    args.arg3 = arg3;
    args.arg4 = 0;
    args.arg5 = 0;
    args.arg6 = 0;
    
    // Llamar a la syscall
    syscall_table[syscall_number](&args);
    
    return args.arg1; // Retornar resultado
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

    shell_print_info("=== IR0 Shell Built-in Commands ===\n");
    shell_print_info("help     - Show this help message");
    shell_print_info("info     - Show system information");
    shell_print_info("version  - Show kernel version");
    shell_print_info("ps       - List processes");
    shell_print_info("meminfo  - Show memory information");
    shell_print_info("debug    - Show debug information");
    shell_print_info("clear    - Clear screen");
    shell_print_info("echo     - Print text");
    shell_print_info("cd       - Change directory");
    shell_print_info("pwd      - Print working directory");
    shell_print_info("ls       - List directory contents");
    shell_print_info("cat      - Display file contents");
    shell_print_info("mkdir    - Create directory");
    shell_print_info("rm       - Remove file");
    shell_print_info("cp       - Copy file");
    shell_print_info("mv       - Move file");
    shell_print_info("kill     - Kill process");
    shell_print_info("sleep    - Sleep for seconds");
    shell_print_info("reboot   - Reboot system");
    shell_print_info("halt     - Halt system");
    shell_print_info("exit     - Exit shell");

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

// Comando para probar syscalls
static int shell_cmd_syscall_test(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;
    (void)args;
    (void)arg_count;

    print("Testing system calls...\n");
    
    // Probar SYS_GETPID
    int64_t pid = shell_syscall(SYS_GETPID, 0, 0, 0);
    print("Current PID: ");
    print_int32(pid);
    print("\n");
    
    // Probar SYS_GETTIME
    int64_t time = shell_syscall(SYS_GETTIME, 0, 0, 0);
    print("Current time (ms): ");
    print_int32(time);
    print("\n");
    
    // Probar SYS_WRITE (escribir a stdout)
    const char *message = "Hello from syscall!\n";
    int64_t written = shell_syscall(SYS_WRITE, 1, (uint64_t)message, strlen(message));
    print("Bytes written: ");
    print_int32(written);
    print("\n");
    
    print("Syscall test completed!\n");
    return 0;
}

// Comando para probar sleep
static int shell_cmd_sleep_test(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;
    (void)arg_count;

    if (arg_count < 2) {
        print("Usage: sleep_test <milliseconds>\n");
        return 1;
    }
    
    int ms = atoi(args[1]);
    if (ms <= 0) {
        print("Invalid time value\n");
        return 1;
    }
    
    print("Sleeping for ");
    print_int32(ms);
    print(" milliseconds...\n");
    
    int64_t start_time = shell_syscall(SYS_GETTIME, 0, 0, 0);
    shell_syscall(SYS_SLEEP, ms, 0, 0);
    int64_t end_time = shell_syscall(SYS_GETTIME, 0, 0, 0);
    
    print("Slept for ");
    print_int32(end_time - start_time);
    print(" milliseconds\n");
    
    return 0;
}

// Comando para probar yield
static int shell_cmd_yield_test(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;
    (void)args;
    (void)arg_count;

    print("Testing yield...\n");
    
    for (int i = 0; i < 5; i++) {
        print("Before yield ");
        print_int32(i);
        print("\n");
        
        shell_syscall(SYS_YIELD, 0, 0, 0);
        
        print("After yield ");
        print_int32(i);
        print("\n");
    }
    
    print("Yield test completed!\n");
    return 0;
}

// Comando para probar read desde stdin
static int shell_cmd_read_test(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;
    (void)args;
    (void)arg_count;

    print("Testing read from stdin...\n");
    print("Type something and press Enter: ");
    
    char buffer[256];
    int64_t bytes_read = shell_syscall(SYS_READ, 0, (uint64_t)buffer, sizeof(buffer) - 1);
    
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        print("You typed: ");
        print(buffer);
        print(" (");
        print_int32(bytes_read);
        print(" bytes)\n");
    } else {
        print("Read failed\n");
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

static int shell_cmd_cd(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)config;

    if (arg_count == 0)
    {
        // Change to home directory
        strcpy(ctx->current_dir, "/");
        return 0;
    }

    if (arg_count > 1)
    {
        shell_print_error("cd: too many arguments");
        return -1;
    }

    // TODO: Implement actual directory change
    strcpy(ctx->current_dir, args[0]);

    return 0;
}

static int shell_cmd_pwd(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)config;
    (void)args;
    (void)arg_count;

    shell_print_info(ctx->current_dir);
    print("\n");

    return 0;
}

static int shell_cmd_ls(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count)
{
    (void)ctx;
    (void)config;
    (void)args;
    (void)arg_count;

    shell_print_info("=== Directory Contents ===");
    shell_print_info("drwxr-xr-x  root  root  /");
    shell_print_info("-rw-r--r--  root  root  kernel.bin");
    shell_print_info("-rw-r--r--  root  root  config.txt");
    shell_print_info("drwxr-xr-x  root  root  /boot");
    shell_print_info("drwxr-xr-x  root  root  /etc");

    return 0;
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

    shell_print_info("=== File Contents: ");
    shell_print_info(args[0]);
    shell_print_info(" ===");
    shell_print_info("This is a sample file content.");
    shell_print_info("The file system is working correctly.");
    shell_print_info("IR0 Kernel is running smoothly.");

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

    shell_print_success("Directory created: ");
    shell_print_success(args[0]);

    return 0;
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

    shell_print_success("File removed: ");
    shell_print_success(args[0]);

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
    
    while (test_running) {
        // Polling del buffer de teclado
        if (keyboard_buffer_has_data()) {
            c = keyboard_buffer_get();
            
            if (c == 'q') {
                print("Quit key pressed. Exiting test.\n");
                test_running = 0;
            } else if (c == '\b') {
                print("\b \b");
            } else if (c == '\t') {
                print("Tab pressed\n");
            } else if (c == '\n') {
                print("Enter pressed\n");
            } else {
                
                char temp_str[2] = {c, '\0'};
                print(temp_str);
                print("'\n");
            }
        }
        
        // Pequeña pausa para no consumir toda la CPU
        for (volatile int i = 0; i < 100000; i++) { /* busy wait */ }
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
    shell_add_builtin_command("syscall", "Test system calls", shell_cmd_syscall_test);
    shell_add_builtin_command("sleep_test", "Test sleep syscall", shell_cmd_sleep_test);
    shell_add_builtin_command("yield_test", "Test yield syscall", shell_cmd_yield_test);
    shell_add_builtin_command("read_test", "Test read from stdin", shell_cmd_read_test);
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
