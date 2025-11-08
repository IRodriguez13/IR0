/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Built-in Shell
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Interactive shell for Ring 3 commands
 */

#include "shell.h"
#include <drivers/video/typewriter.h>
#include <ir0/syscall.h>
#include <ir0/memory/kmem.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// kernel/shell.c: use public headers for functions (kmem/string)

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_BUFFER ((volatile uint16_t *)0xB8000)

int cursor_pos = 0; /* Made global for typewriter access */

static void vga_putchar(char c, uint8_t color)
{
  if (c == '\n')
  {
    cursor_pos = (cursor_pos / VGA_WIDTH + 1) * VGA_WIDTH;
    if (cursor_pos >= VGA_WIDTH * VGA_HEIGHT)
    {
      /* Scroll screen */
      for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++)
        VGA_BUFFER[i] = VGA_BUFFER[i + VGA_WIDTH];
      for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH;
           i++)
        VGA_BUFFER[i] = 0x0F20;
      cursor_pos = (VGA_HEIGHT - 1) * VGA_WIDTH;
    }
  }
  else if (c == '\b')
  {
    if (cursor_pos > 0)
    {
      cursor_pos--;
      VGA_BUFFER[cursor_pos] = (color << 8) | ' ';
    }
  }
  else
  {
    VGA_BUFFER[cursor_pos] = (color << 8) | c;
    cursor_pos++;
    if (cursor_pos >= VGA_WIDTH * VGA_HEIGHT)
    {
      /* Scroll screen */
      for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++)
        VGA_BUFFER[i] = VGA_BUFFER[i + VGA_WIDTH];
      for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH;
           i++)
        VGA_BUFFER[i] = 0x0F20;
      cursor_pos = (VGA_HEIGHT - 1) * VGA_WIDTH;
    }
  }
}

static void vga_print(const char *str, uint8_t color)
{
  while (*str)
    vga_putchar(*str++, color);
}

static void vga_clear(void)
{
  for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
    VGA_BUFFER[i] = 0x0F20;
  cursor_pos = 0;
}

static int str_starts_with(const char *str, const char *prefix)
{
  while (*prefix)
  {
    if (*str++ != *prefix++)
      return 0;
  }
  return 1;
}

static const char *skip_whitespace(const char *str)
{
  while (*str == ' ' || *str == '\t')
    str++;
  return str;
}
/* Forward declarations for command handlers used in the table */
static void cmd_list_help(void);
static void cmd_mount(const char *args);

static void cmd_help(void)
{
  typewriter_vga_print("IR0 Shell - Available commands:\n", 0x0F);
  /* commands table will drive help output */
  cmd_list_help();
}

static void cmd_clear(void)
{
  vga_clear();

  /* Show banner after clear */
  typewriter_vga_print("IR0 Shell v0.0.1 pre-release 1\n", 0x0B);
  typewriter_vga_print("Type 'help' for available commands\n\n", 0x07);
}

static void cmd_ls(const char *args)
{
  int detailed = 0;
  const char *path;

  if (!args || *args == '\0')
  {
    path = "/";
  }
  else if (str_starts_with(args, "-l "))
  {
    detailed = 1;
    path = skip_whitespace(args + 2);
    if (*path == '\0')
      path = "/";
  }
  else if (str_starts_with(args, "-l"))
  {
    detailed = 1;
    path = "/";
  }
  else
  {
    path = args;
  }

  if (detailed)
    syscall(SYS_LS_DETAILED, (uint64_t)path, 0, 0);
  else
    syscall(SYS_LS, (uint64_t)path, 0, 0);
}

static void cmd_cat(const char *filename)
{
  if (!filename || *filename == '\0')
  {
    typewriter_vga_print("Usage: cat <filename>\n", 0x0C);
    return;
  }

  syscall(SYS_CAT, (uint64_t)filename, 0, 0);
}

static void cmd_mkdir(const char *dirname)
{
  if (!dirname || *dirname == '\0')
  {
    typewriter_vga_print("Usage: mkdir <dirname>\n", 0x0C);
    return;
  }

  int64_t result = syscall(SYS_MKDIR, (uint64_t)dirname, 0755, 0);
  if (result < 0)
    typewriter_vga_print("mkdir: failed\n", 0x0C);
}

static void cmd_rmdir(const char *dirname)
{
  if (!dirname || *dirname == '\0')
  {
    typewriter_vga_print("Usage: rmdir <dirname>\n", 0x0C);
    return;
  }

  int64_t result = syscall(SYS_RMDIR, (uint64_t)dirname, 0, 0);
  if (result < 0)
    typewriter_vga_print("rmdir: failed\n", 0x0C);
}

static void cmd_ps(void)
{
  /* Use real process listing syscall */
  syscall(SYS_PS, 0, 0, 0);
}

static void cmd_echo(const char *text)
{
  if (!text || *text == '\0')
  {
    typewriter_vga_print("\n", 0x0F);
    return;
  }

  // Check for output redirection (>)
  const char *redirect_pos = strstr(text, " > ");

  if (redirect_pos)
  {
    // Extract the text before '>'
    size_t text_len = redirect_pos - text;
    char *content = (char *)kmalloc(text_len + 2); // +1 for newline, +1 for null
    if (!content)
    {
      typewriter_vga_print("Error: Out of memory\n", 0x0C);
      return;
    }

    // Copy text content
    for (size_t i = 0; i < text_len; i++)
    {
      content[i] = text[i];
    }
    content[text_len] = '\n';
    content[text_len + 1] = '\0';

    // Get filename (skip " > ")
    const char *filename = skip_whitespace(redirect_pos + 3);

    if (*filename == '\0')
    {
      typewriter_vga_print("Error: No filename specified\n", 0x0C);
      kfree(content);
      return;
    }

    // Write to file using syscall
    int64_t result = syscall(SYS_WRITE_FILE, (uint64_t)filename, (uint64_t)content, 0);

    if (result < 0)
    {
      typewriter_vga_print("Error: Could not write to file '", 0x0C);
      typewriter_vga_print(filename, 0x0C);
      typewriter_vga_print("'\n", 0x0C);
    }
    else
    {
      typewriter_vga_print("Written to '", 0x0A);
      typewriter_vga_print(filename, 0x0A);
      typewriter_vga_print("'\n", 0x0A);
    }

    kfree(content);
  }
  else
  {
    // No redirection, just print to screen
    typewriter_vga_print(text, 0x0F);
    typewriter_vga_print("\n", 0x0F);
  }
}

static void cmd_exec(const char *filename)
{
  if (!filename || *filename == '\0')
  {
    typewriter_vga_print("Usage: exec <filename>\n", 0x0C);
    return;
  }

  int64_t result = syscall(SYS_EXEC, (uint64_t)filename, 0, 0);
  if (result < 0)
    typewriter_vga_print("exec: failed\n", 0x0C);
}

static void cmd_exit(void) { syscall(SYS_EXIT, 0, 0, 0); }

// Helper function to perform text substitution
static char *perform_substitution(const char *original, size_t original_size,
                                  const char *old_str, const char *new_str)
{
  if (!original || !old_str || !new_str)
  {
    return NULL;
  }

  int old_len = 0, new_len = 0;

  // Calculate string lengths
  while (old_str[old_len])
    old_len++;
  while (new_str[new_len])
    new_len++;

  if (old_len == 0)
  {
    return NULL; // Can't replace empty string
  }

  // Count occurrences of old_str in original
  int count = 0;
  const char *pos = original;
  while (pos && (pos = strstr(pos, old_str)) != NULL)
  {
    count++;
    pos += old_len;
  }

  if (count == 0)
  {
    // No replacements needed, return copy of original
    char *result = (char *)kmalloc(original_size + 1);
    if (!result)
      return NULL;

    for (size_t i = 0; i < original_size; i++)
    {
      result[i] = original[i];
    }
    result[original_size] = '\0';
    return result;
  }

  // Calculate new size
  size_t new_size = original_size + count * (new_len - old_len);
  char *result = (char *)kmalloc(new_size + 1);
  if (!result)
  {
    return NULL;
  }

  // Perform substitution
  const char *src = original;
  char *dst = result;

  while (*src)
  {
    // Check if we found old_str at current position
    int match = 1;
    for (int i = 0; i < old_len && src[i]; i++)
    {
      if (src[i] != old_str[i])
      {
        match = 0;
        break;
      }
    }

    if (match && (src + old_len <= original + original_size))
    {
      // Copy new_str
      for (int i = 0; i < new_len; i++)
      {
        *dst++ = new_str[i];
      }
      src += old_len; // Skip old_str
    }
    else
    {
      // Copy original character
      *dst++ = *src++;
    }
  }

  *dst = '\0';
  return result;
}

static void cmd_sed(const char *args)
{
  if (!args || *args == '\0')
  {
    typewriter_vga_print("Usage: sed 's/OLD/NEW/' FILE\n", 0x0C);
    typewriter_vga_print("Example: sed 's/hello/world/' myfile.txt\n", 0x07);
    return;
  }

  // Parse sed command: s/OLD/NEW/ FILE
  if (!str_starts_with(args, "s/"))
  {
    typewriter_vga_print(
        "Error: Only substitute command 's/OLD/NEW/' supported\n", 0x0C);
    return;
  }

  // Find the pattern: s/OLD/NEW/
  const char *pattern_start = args + 2; // Skip "s/"
  const char *old_end = NULL;
  const char *new_start = NULL;
  const char *new_end = NULL;
  const char *filename = NULL;

  // Find first '/' (end of OLD)
  for (const char *p = pattern_start; *p; p++)
  {
    if (*p == '/')
    {
      old_end = p;
      new_start = p + 1;
      break;
    }
  }

  if (!old_end)
  {
    typewriter_vga_print("Error: Invalid sed pattern. Use 's/OLD/NEW/'\n",
                         0x0C);
    return;
  }

  // Find second '/' (end of NEW)
  for (const char *p = new_start; *p; p++)
  {
    if (*p == '/')
    {
      new_end = p;
      filename = skip_whitespace(p + 1);
      break;
    }
  }

  if (!new_end || !filename || *filename == '\0')
  {
    typewriter_vga_print("Error: Invalid sed pattern or missing filename\n",
                         0x0C);
    return;
  }

  // Extract OLD and NEW strings
  char old_str[256], new_str[256];
  int old_len = old_end - pattern_start;
  int new_len = new_end - new_start;

  if (old_len >= 255 || new_len >= 255)
  {
    typewriter_vga_print("Error: Pattern too long\n", 0x0C);
    return;
  }

  // Copy strings
  for (int i = 0; i < old_len; i++)
  {
    old_str[i] = pattern_start[i];
  }
  old_str[old_len] = '\0';

  for (int i = 0; i < new_len; i++)
  {
    new_str[i] = new_start[i];
  }
  new_str[new_len] = '\0';

  // Read the file content
  void *file_data = NULL;
  size_t file_size = 0;

  int64_t result = syscall(SYS_READ_FILE, (uint64_t)filename, (uint64_t)&file_data, (uint64_t)&file_size);

  if (result < 0)
  {
    typewriter_vga_print("Error: Could not read file '", 0x0C);
    typewriter_vga_print(filename, 0x0C);
    typewriter_vga_print("'\n", 0x0C);
    return;
  }

  if (!file_data || file_size == 0)
  {
    typewriter_vga_print("Error: File is empty or could not be read\n", 0x0C);
    return;
  }

  // Perform text substitution
  char *original = (char *)file_data;
  char *modified = perform_substitution(original, file_size, old_str, new_str);

  if (!modified)
  {
    typewriter_vga_print("Error: Could not perform substitution\n", 0x0C);
    // Free the original file data (would need kfree syscall)
    return;
  }

  // Write the modified content back to the file
  result = syscall(SYS_WRITE_FILE, (uint64_t)filename, (uint64_t)modified, 0);

  if (result < 0)
  {
    typewriter_vga_print("Error: Could not write to file '", 0x0C);
    typewriter_vga_print(filename, 0x0C);
    typewriter_vga_print("'\n", 0x0C);
  }
  else
  {
    typewriter_vga_print("Successfully replaced '", 0x0A);
    typewriter_vga_print(old_str, 0x0A);
    typewriter_vga_print("' with '", 0x0A);
    typewriter_vga_print(new_str, 0x0A);
    typewriter_vga_print("' in '", 0x0A);
    typewriter_vga_print(filename, 0x0A);
    typewriter_vga_print("'\n", 0x0A);
  }

  // TODO: Free memory (need kfree syscall or memory management)
}

static void cmd_type(const char *mode)
{
  if (!mode || *mode == '\0')
  {
    /* Show current mode */
    typewriter_mode_t current = typewriter_get_mode();
    typewriter_vga_print("Current typewriter mode: ", 0x0F);
    switch (current)
    {
    case TYPEWRITER_DISABLED:
      typewriter_vga_print("off\n", 0x0F);
      break;
    case TYPEWRITER_FAST:
      typewriter_vga_print("fast\n", 0x0F);
      break;
    case TYPEWRITER_NORMAL:
      typewriter_vga_print("normal\n", 0x0F);
      break;
    case TYPEWRITER_SLOW:
      typewriter_vga_print("slow\n", 0x0F);
      break;
    }
    typewriter_vga_print("Available modes: fast, normal, slow, off\n", 0x07);
    return;
  }

  if (str_starts_with(mode, "fast"))
  {
    typewriter_set_mode(TYPEWRITER_FAST);
    typewriter_vga_print("Typewriter mode set to: fast\n", 0x0A);
  }
  else if (str_starts_with(mode, "normal"))
  {
    typewriter_set_mode(TYPEWRITER_NORMAL);
    typewriter_vga_print("Typewriter mode set to: normal\n", 0x0A);
  }
  else if (str_starts_with(mode, "slow"))
  {
    typewriter_set_mode(TYPEWRITER_SLOW);
    typewriter_vga_print("Typewriter mode set to: slow\n", 0x0A);
  }
  else if (str_starts_with(mode, "off"))
  {
    typewriter_set_mode(TYPEWRITER_DISABLED);
    typewriter_vga_print("Typewriter effect disabled\n", 0x0A);
  }
  else
  {
    typewriter_vga_print("Invalid mode. Available: fast, normal, slow, off\n",
                         0x0C);
  }
}

/* Mount command: mount <device> <mountpoint> [fstype] */
static void cmd_mount(const char *args)
{
  if (!args || *args == '\0')
  {
    typewriter_vga_print("Usage: mount <device> <mountpoint> [fstype]\n", 0x0C);
    return;
  }

  // Parse arguments: device mountpoint [fstype]
  // copy to local buffer to tokenize safely
  char buf[256];
  size_t i = 0;
  while (i < sizeof(buf) - 1 && args[i] && args[i] != '\n')
  {
    buf[i] = args[i];
    i++;
  }
  buf[i] = '\0';

  // find tokens
  char *p = buf;
  while (*p == ' ' || *p == '\t')
    p++;
  if (*p == '\0')
  {
    typewriter_vga_print("Usage: mount <device> <mountpoint> [fstype]\n", 0x0C);
    return;
  }
  char *dev = p;
  while (*p && *p != ' ' && *p != '\t')
    p++;
  if (*p)
  {
    *p++ = '\0';
  }
  while (*p == ' ' || *p == '\t')
    p++;
  if (*p == '\0')
  {
    typewriter_vga_print("Usage: mount <device> <mountpoint> [fstype]\n", 0x0C);
    return;
  }
  char *mountpoint = p;
  while (*p && *p != ' ' && *p != '\t')
    p++;
  if (*p)
  {
    *p++ = '\0';
  }
  while (*p == ' ' || *p == '\t')
    p++;
  char *fstype = NULL;
  if (*p)
  {
    fstype = p;
  }

  int64_t ret = syscall(SYS_MOUNT, (uint64_t)dev, (uint64_t)mountpoint, (uint64_t)(fstype ? fstype : 0));
  if (ret < 0)
  {
    typewriter_vga_print("mount: failed\n", 0x0C);
  }
  else
  {
    typewriter_vga_print("mount: success\n", 0x0A);
  }
}

static void cmd_cd(const char *dirname)
{
  if (!dirname || *dirname == '\0')
    dirname = "/";

  int64_t result = syscall(SYS_CHDIR, (uint64_t)dirname, 0, 0);
  if (result < 0)
    typewriter_vga_print("cd: failed\n", 0x0C);
}

static void cmd_pwd(void)
{
  char cwd[256];
  int64_t result = syscall(SYS_GETCWD, (uint64_t)cwd, sizeof(cwd), 0);
  if (result >= 0)
  {
    typewriter_vga_print(cwd, 0x0F);
    typewriter_vga_print("\n", 0x0F);
  }
  else
  {
    typewriter_vga_print("pwd: failed\n", 0x0C);
  }
}

static void cmd_rm(const char *args)
{
  int recursive = 0;
  const char *filename;

  if (!args || *args == '\0')
  {
    typewriter_vga_print("Usage: rm [-r] <filename>\n", 0x0C);
    return;
  }

  /* Check for -r flag */
  if (str_starts_with(args, "-r "))
  {
    recursive = 1;
    filename = skip_whitespace(args + 2);
  }
  else if (str_starts_with(args, "-rf "))
  {
    recursive = 1;
    filename = skip_whitespace(args + 3);
  }
  else
  {
    filename = args;
  }

  if (!filename || *filename == '\0')
  {
    typewriter_vga_print("Usage: rm [-r] <filename>\n", 0x0C);
    return;
  }

  int64_t result;

  /* If recursive flag is set, use recursive removal */
  if (recursive)
  {
    result = syscall(SYS_RMDIR_R, (uint64_t)filename, 0, 0);
    if (result < 0)
    {
      typewriter_vga_print("rm: cannot remove '", 0x0C);
      typewriter_vga_print(filename, 0x0C);
      typewriter_vga_print("': Failed to remove recursively\n", 0x0C);
    }
  }
  else
  {
    /* Try to remove as file */
    result = syscall(SYS_UNLINK, (uint64_t)filename, 0, 0);
    if (result < 0)
    {
      typewriter_vga_print("rm: cannot remove '", 0x0C);
      typewriter_vga_print(filename, 0x0C);
      typewriter_vga_print("': No such file or directory\n", 0x0C);
      typewriter_vga_print("Hint: Use 'rm -r' for directories\n", 0x0C);
    }
  }
}
/* Command table: name, handler, description */
struct shell_cmd
{
  const char *name;
  void (*handler)(const char *args);
  const char *usage;
  const char *desc;
};

static const struct shell_cmd commands[] = {
    {"help", (void (*)(const char *))cmd_help, "help", "Show help"},
    {"clear", (void (*)(const char *))cmd_clear, "clear", "Clear screen"},
    {"ls", cmd_ls, "ls [-l] [DIR]", "List directory"},
    {"cat", cmd_cat, "cat FILE", "Print file"},
    {"mkdir", cmd_mkdir, "mkdir DIR", "Create directory"},
    {"rmdir", cmd_rmdir, "rmdir DIR", "Remove directory"},
    {"rm", cmd_rm, "rm [-r] FILE", "Remove file or dir"},
    {"cd", cmd_cd, "cd [DIR]", "Change directory"},
    {"pwd", (void (*)(const char *))cmd_pwd, "pwd", "Print working directory"},
    {"ps", (void (*)(const char *))cmd_ps, "ps", "List processes"},
    {"echo", cmd_echo, "echo TEXT", "Print text or write to file"},
    {"exec", cmd_exec, "exec FILE", "Execute binary"},
    {"sed", cmd_sed, "sed 's/OLD/NEW/' FILE", "Substitute text in file"},
    {"type", cmd_type, "type [mode]", "Typewriter effect control"},
    {"mount", cmd_mount, "mount DEV MOUNTPOINT [fstype]", "Mount filesystem"},
    {"exit", (void (*)(const char *))cmd_exit, "exit", "Exit shell"},
};

static void cmd_list_help(void)
{
  for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
  {
    typewriter_vga_print("  ", 0x0F);
    typewriter_vga_print(commands[i].usage, 0x0F);
    typewriter_vga_print(" - ", 0x0F);
    typewriter_vga_print(commands[i].desc, 0x0F);
    typewriter_vga_print("\n", 0x0F);
  }
}

static void execute_command(const char *cmd)
{
  char buf[256];
  size_t i = 0;
  cmd = skip_whitespace(cmd);
  if (*cmd == '\0')
    return;
  while (i < sizeof(buf) - 1 && *cmd && *cmd != '\n')
    buf[i++] = *cmd++;
  buf[i] = '\0';

  // Extract command name
  char *p = buf;
  while (*p && *p != ' ' && *p != '\t')
    p++;
  const char *rest = NULL;
  if (*p)
  {
    *p++ = '\0';
    rest = skip_whitespace(p);
  }

  // Lookup command
  for (size_t j = 0; j < sizeof(commands) / sizeof(commands[0]); j++)
  {
    if (strcmp(buf, commands[j].name) == 0)
    {
      commands[j].handler(rest);
      return;
    }
  }

  typewriter_vga_print("Unknown command: ", 0x0C);
  typewriter_vga_print(buf, 0x0C);
  typewriter_vga_print("\nType 'help' for available commands\n", 0x0C);
}

void shell_entry(void)
{
  char input[256];
  int input_pos = 0;

  vga_clear();
  vga_print("IR0 Shell v0.0.1 pre-release 1\n", 0x0B);
  vga_print("Type 'help' for available commands\n\n", 0x07);

  for (;;)
  {
    /* Print prompt */
    vga_print("~$ ", 0x0A);

    /* Read input */
    input_pos = 0;
    while (1)
    {
      char c;
      int64_t n = syscall(SYS_READ, 0, (uint64_t)&c, 1);

      if (n <= 0)
        continue;

      if (c == '\n')
      {
        vga_putchar('\n', 0x0F);
        input[input_pos] = '\0';
        break;
      }
      else if (c == '\b' || c == 127)
      {
        if (input_pos > 0)
        {
          input_pos--;
          vga_putchar('\b', 0x0F);
        }
      }
      else if (c >= 32 && c < 127 && input_pos < 255)
      {
        input[input_pos++] = c;
        vga_putchar(c, 0x0F);
      }
    }

    /* Execute command */
    execute_command(input);
  }
}
