#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>

// ===============================================================================
// SHELL CONSTANTS
// ===============================================================================

#define SHELL_MAX_LINE_LENGTH 1024
#define SHELL_MAX_ARGS 64
#define SHELL_MAX_ARG_LENGTH 256
#define SHELL_MAX_COMMAND_LENGTH 256
#define SHELL_MAX_HISTORY 100
#define SHELL_MAX_PROMPT_LENGTH 256
#define SHELL_MAX_BUILTIN_COMMANDS 32

// Shell colors
#define SHELL_COLOR_RED     "\033[31m"
#define SHELL_COLOR_GREEN   "\033[32m"
#define SHELL_COLOR_YELLOW  "\033[33m"
#define SHELL_COLOR_BLUE    "\033[34m"
#define SHELL_COLOR_MAGENTA "\033[35m"
#define SHELL_COLOR_CYAN    "\033[36m"
#define SHELL_COLOR_WHITE   "\033[37m"
#define SHELL_COLOR_RESET   "\033[0m"

// Prompt formats
#define SHELL_PROMPT_DEFAULT "[IR0:%s]$ "
#define SHELL_PROMPT_SIMPLE  "$ "
#define SHELL_PROMPT_FULL    "[%u@%h %w]$ "

// ===============================================================================
// SHELL DATA STRUCTURES
// ===============================================================================

typedef struct 
{
    char current_dir[256];
    char prompt[SHELL_MAX_PROMPT_LENGTH];
    char history[SHELL_MAX_HISTORY][SHELL_MAX_LINE_LENGTH];
    int history_count;
    int history_index;
    int running;
    int exit_code;
} shell_context_t;

typedef struct 
{
    int enable_history;
    int enable_colors;
    int max_line_length;
    int prompt_format;
    char prompt[256];
    int max_history;
    int colors_enabled;
} shell_config_t;

typedef struct 
{
    char *name;
    char *description;
    int (*handler)(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count);
} shell_command_t;

// ===============================================================================
// SHELL CORE FUNCTIONS
// ===============================================================================

// Initialize the shell
int shell_init(shell_context_t *ctx, shell_config_t *config);

// Run the shell main loop
int shell_run(shell_context_t *ctx, shell_config_t *config);

// Process a command line
int shell_process_line(shell_context_t *ctx, shell_config_t *config, const char *line);

// Parse command line arguments
int shell_parse_args(const char *line, char *argv[], int max_args);

// Parse command
int shell_parse_command(const char *line, char *command, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int *arg_count);

// Execute command
int shell_execute_command(shell_context_t *ctx, shell_config_t *config, const char *command, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count);

// Add to history
void shell_add_to_history(shell_context_t *ctx, const char *line);

// Type alias for command handler
typedef int (*shell_command_handler_t)(shell_context_t *ctx, shell_config_t *config, char args[SHELL_MAX_ARGS][SHELL_MAX_ARG_LENGTH], int arg_count);

// Initialize builtin commands (implemented in shell.c)
void shell_init_builtin_commands(void);

// Add builtin command (implemented in shell.c)
void shell_add_builtin_command(const char *name, const char *description, shell_command_handler_t handler);

// Register a new command
int shell_register_command(const char *name, const char *description, int (*handler)(int argc, char *argv[]));

// Get shell context
shell_context_t *shell_get_context(void);

// Update shell prompt
void shell_update_prompt(void);

// Start the shell (main entry point)
void shell_start(void);

// ===============================================================================
// SHELL HISTORY FUNCTIONS
// ===============================================================================

// Add line to history
void shell_add_history(const char *line);

// Get history entry by index
const char *shell_get_history(int index);

// ===============================================================================
// BUILT-IN COMMANDS
// ===============================================================================

// Built-in commands are implemented as static functions in shell.c
// and are registered through shell_init_builtin_commands()

// ===============================================================================
// SHELL UTILITY FUNCTIONS
// ===============================================================================

// Basic print function
void shell_print(const char *message);

// Print functions with color support
void shell_print_color(const char *message, uint8_t color);
void shell_print_error(const char *message);
void shell_print_success(const char *message);
void shell_print_warning(const char *message);
void shell_print_info(const char *message);

// ===============================================================================
// SHELL CONFIGURATION FUNCTIONS
// ===============================================================================

// Set shell configuration
void shell_set_config(const shell_config_t *config);

// Get shell configuration
void shell_get_config(shell_config_t *config);

// ===============================================================================
// SHELL PLUGIN SYSTEM
// ===============================================================================

// Load shell plugin
int shell_load_plugin(const char *plugin_path);

// Unload shell plugin
int shell_unload_plugin(const char *plugin_name);

// ===============================================================================
// SHELL SCRIPTING
// ===============================================================================

// Execute shell script
int shell_execute_script(const char *script_path);

// Parse shell script
int shell_parse_script(const char *script_content);

// ===============================================================================
// SHELL ENVIRONMENT
// ===============================================================================

// Set environment variable
int shell_set_env(const char *name, const char *value);

// Get environment variable
const char *shell_get_env(const char *name);

// Unset environment variable
int shell_unset_env(const char *name);

// ===============================================================================
// SHELL ALIASES
// ===============================================================================

// Set command alias
int shell_set_alias(const char *alias, const char *command);

// Get command alias
const char *shell_get_alias(const char *alias);

// Remove command alias
int shell_remove_alias(const char *alias);

// Expand aliases in command
char *shell_expand_aliases(const char *command);

// ===============================================================================
// SHELL GLOBAL VARIABLES
// ===============================================================================

// Builtin commands array and count
extern shell_command_t shell_builtin_commands[];
extern int shell_builtin_count;

#endif // SHELL_H
