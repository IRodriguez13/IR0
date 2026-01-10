/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Built-in DebShell
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Interactive shell for Ring 3 commands
 */

#include "dbgshell.h"
#include <drivers/video/typewriter.h>
#include <ir0/syscall.h>
#include <ir0/fcntl.h>
#include <ir0/memory/kmem.h>
#include <ir0/vga.h>
#include <ir0/types.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <drivers/serial/serial.h>
#include <ir0/stat.h>
#include <ir0/chmod.h>
#include <drivers/storage/ata.h>
#include <drivers/storage/fs_types.h>
#include "kernel/syscalls.h" // Ensure SYS_GET_BLOCK_DEVICES is included
#include <ir0/net.h>

/* kernel/shell.c: use public headers for functions (kmem/string) */

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_BUFFER ((volatile uint16_t *)0xB8000)

int cursor_pos = 0; /* Made global for typewriter access */

static void shell_write(int fd, const char *str)
{
  if (!str)
    return;
  
  uint8_t color = (fd == 2) ? 0x0C : 0x0F;
  
  if (fd == 1 || fd == 2)
  {
    typewriter_vga_print(str, color);
  }
}

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

void vga_print(const char *str, uint8_t color)
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

/* Human-readable size: prints bytes in B, KB, MB, GB with suffix */
[[maybe_unused]]static void shell_print_hr_size(uint64_t bytes)
{
  const char *units[] = {"B", "KB", "MB", "GB", "TB"};
  double value = (double)bytes;
  int unit = 0;
  while (value >= 1024.0 && unit < 4)
  {
    value /= 1024.0;
    unit++;
  }
  /* Print with one decimal if >= 10, else two decimals for small numbers */
  char buf[32];
  if (value >= 100.0)
    snprintf(buf, sizeof(buf), "%.0f%s", value, units[unit]);
  else if (value >= 10.0)
    snprintf(buf, sizeof(buf), "%.1f%s", value, units[unit]);
  else
    snprintf(buf, sizeof(buf), "%.2f%s", value, units[unit]);
  typewriter_vga_print(buf, 0x0F);
}
/* Forward declarations for command handlers used in the table */
static void cmd_list_help(void);
static void cmd_mount(const char *args);
static void cmd_touch(const char *filename);

static void cmd_help(void)
{
  shell_write(1, "IR0 Dbgshell - Available commands:\n");
  /* commands table will drive help output */
  cmd_list_help();
}

static void cmd_whoami(void)
{
  syscall(94, 0, 0, 0); // SYS_WHOAMI
}

void cmd_clear(void)
{
  vga_clear();

  /* Show banner after clear */
  typewriter_vga_print("IR0 DebShell v0.0.1 pre-release 1\n", 0x0B);
  typewriter_vga_print("Type 'help' for available commands\n\n", 0x07);
}

static void cmd_ls(const char *args)
{
  int detailed = 0;
  const char *path;
  char cwd[256];

  if (!args || *args == '\0')
  {
    /* Get current working directory */
    if (syscall(SYS_GETCWD, (uint64_t)cwd, sizeof(cwd), 0) >= 0)
      path = cwd;
    else
      path = "/";
  }
  else if (str_starts_with(args, "-l "))
  {
    detailed = 1;
    path = skip_whitespace(args + 2);
    if (*path == '\0')
    {
      /* Get current working directory */
      if (syscall(SYS_GETCWD, (uint64_t)cwd, sizeof(cwd), 0) >= 0)
        path = cwd;
      else
        path = "/";
    }
  }
  else if (str_starts_with(args, "-l"))
  {
    detailed = 1;
    /* Get current working directory */
    if (syscall(SYS_GETCWD, (uint64_t)cwd, sizeof(cwd), 0) >= 0)
      path = cwd;
    else
      path = "/";
  }
  else
  {
    path = args;
  }

  if (detailed)
    ir0_ls(path);
  else
    ir0_ls(path);
}

static void cmd_cat(const char *filename)
{
  if (!filename || *filename == '\0')
  {
    shell_write(2, "Usage: cat <filename>\n");
    return;
  }

  int fd = ir0_open(filename, O_RDONLY, 0);
  if (fd < 0)
  {
    shell_write(2, "cat: cannot open '");
    shell_write(2, filename);
    shell_write(2, "'\n");
    return;
  }

  char buffer[512];
  int max_iterations = 1000; /* Safety limit to prevent infinite loops */
  int iteration = 0;
  
  for (;;)
  {
    if (iteration >= max_iterations)
    {
      shell_write(2, "cat: too many iterations, possible infinite loop\n");
      break;
    }
    
    int64_t bytes_read = ir0_read(fd, buffer, sizeof(buffer));
    
    if (bytes_read <= 0)
      break;
    
    syscall(SYS_WRITE, STDOUT_FILENO, (uint64_t)buffer, (uint64_t)bytes_read);
    iteration++;
  }

  ir0_close(fd);
}

static void cmd_mkdir(const char *dirname)
{
  if (!dirname || *dirname == '\0')
  {
    shell_write(2, "Usage: mkdir <dirname>\n");
    return;
  }

  int64_t result = ir0_mkdir(dirname, 0755);
  if (result < 0)
    shell_write(2, "mkdir: failed\n");
}

static void cmd_rmdir(const char *args)
{
  if (!args || *args == '\0')
  {
    shell_write(2, "Usage: rmdir [-e] <dirname>\n");
    return;
  }

  int force = 0;
  const char *dirname = args;

  /* Check for -e flag */
  if (args[0] == '-')
  {
    int i = 1;
    while (args[i] && args[i] != ' ' && args[i] != '\t')
    {
      if (args[i] == 'e')
        force = 1;
      i++;
    }
    if (force)
    {
      dirname = skip_whitespace(args + i);
    }
    else
    {
      dirname = skip_whitespace(args + i);
    }
  }

  if (!dirname || *dirname == '\0')
  {
    shell_write(2, "Usage: rmdir [-e] <dirname>\n");
    return;
  }

  int64_t result;
  if (force)
  {
    result = ir0_rmdir(dirname);
  }
  else
  {
    result = ir0_rmdir(dirname);
  }

  if (result < 0)
    shell_write(2, "rmdir: failed\n");
}

static void cmd_ps(void)
{
  cmd_cat("/proc/ps");
}

static void cmd_echo(const char *text)
{
  if (!text || *text == '\0')
  {
    shell_write(1, "\n");
    return;
  }

  /* Check for output redirection: support '>', ' > ', '>>', ' >> ' */
  const char *redir = NULL;
  int append = 0;

  /* Search for '>>' first (append), then single '>' (overwrite) */
  const char *pos = strstr(text, ">>");
  if (pos)
  {
    redir = pos;
    append = 1;
  }
  else
  {
    pos = strstr(text, ">");
    if (pos)
    {
      redir = pos;
      append = 0;
    }
  }

  if (redir)
  {
    /* Extract message (trim trailing space before redir) */
    size_t msg_len = (size_t)(redir - text);
    while (msg_len > 0 && (text[msg_len - 1] == ' ' || text[msg_len - 1] == '\t'))
      msg_len--;

    /* Build content (message + newline) */
    char *new_content = (char *)kmalloc(msg_len + 2);
    if (!new_content)
    {
      typewriter_vga_print("Error: Out of memory\n", 0x0C);
      return;
    }
    for (size_t i = 0; i < msg_len; i++)
      new_content[i] = text[i];
    new_content[msg_len] = '\n';
    new_content[msg_len + 1] = '\0';

    /* Determine filename start (skip redir token and whitespace) */
    const char *fname_start = redir;
    /* Skip '>>' or '>' tokens */
    if (append && fname_start[0] == '>' && fname_start[1] == '>')
      fname_start += 2;
    else if (!append && fname_start[0] == '>')
      fname_start += 1;

    fname_start = skip_whitespace(fname_start);
    if (*fname_start == '\0')
    {
      typewriter_vga_print("Error: No filename specified\n", 0x0C);
      kfree(new_content);
      return;
    }

    /* Normalize path: convert relative paths to absolute paths */
    /* If path doesn't start with '/', prepend '/' to make it absolute */
    char normalized_path[256];
    if (fname_start[0] == '/')
    {
      /* Already absolute path */
      size_t len = strlen(fname_start);
      if (len >= sizeof(normalized_path))
      {
        typewriter_vga_print("Error: Path too long\n", 0x0C);
        kfree(new_content);
        return;
      }
      strcpy(normalized_path, fname_start);
    }
    else
    {
      /* Relative path: make it absolute by prepending '/' */
      size_t len = strlen(fname_start);
      if (len + 1 >= sizeof(normalized_path))
      {
        typewriter_vga_print("Error: Path too long\n", 0x0C);
        kfree(new_content);
        return;
      }
      normalized_path[0] = '/';
      strcpy(normalized_path + 1, fname_start);
    }

    /* If append requested, read existing file and concatenate */
    if (append)
    {
      /* Prevent appending to directories or root */
      if (strcmp(normalized_path, "/") == 0)
      {
        typewriter_vga_print("Error: Refusing to write to root '/'\n", 0x0C);
        kfree(new_content);
        return;
      }
      stat_t st;
      int64_t sret = syscall(SYS_STAT, (uint64_t)normalized_path, (uint64_t)&st, 0);
      if (sret == 0 && S_ISDIR(st.st_mode))
      {
        typewriter_vga_print("Error: Refusing to write to a directory\n", 0x0C);
        kfree(new_content);
        return;
      }

      void *old_data = NULL;
      size_t old_size = 0;
      int fd = ir0_open(normalized_path, O_RDONLY, 0);
      if (fd >= 0) {
        /* Get file size first */
        stat_t st;
        if (ir0_fstat(fd, &st) >= 0) {
          old_size = st.st_size;
          if (old_size > 0) {
            old_data = kmalloc(old_size);
            if (old_data) {
              int64_t r = ir0_read(fd, old_data, old_size);
              ir0_close(fd);
              if (r < 0) {
                kfree(old_data);
                old_data = NULL;
                old_size = 0;
              }
            }
          }
        } else {
          ir0_close(fd);
        }
      }
      if (old_data && old_size > 0)
      {
        /* Allocate combined buffer */
        size_t total = old_size + (msg_len + 1);
        char *combined = (char *)kmalloc(total + 1);
        if (!combined)
        {
          typewriter_vga_print("Error: Out of memory\n", 0x0C);
          /* Free old_data if allocated by FS */
          if (old_data)
            kfree(old_data);
          kfree(new_content);
          return;
        }
        /* Copy old data then new content */
        for (size_t i = 0; i < old_size; i++)
          combined[i] = ((char *)old_data)[i];
        for (size_t i = 0; i < msg_len + 1; i++)
          combined[old_size + i] = new_content[i];
        combined[total] = '\0';

        /* Write back */
        int fd = ir0_open(normalized_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
          int64_t w = ir0_write(fd, combined, total);
          ir0_close(fd);
          if (w < 0)
          {
            typewriter_vga_print("Error: Could not write to file '", 0x0C);
            typewriter_vga_print(normalized_path, 0x0C);
            typewriter_vga_print("'\n", 0x0C);
          }
          else
          {
            typewriter_vga_print("Written to '", 0x0A);
            typewriter_vga_print(normalized_path, 0x0A);
            typewriter_vga_print("'\n", 0x0A);
          }
        } else {
          typewriter_vga_print("Error: Could not open file for writing\n", 0x0C);
        }

        kfree(combined);
        if (old_data)
          kfree(old_data);
      }
      else
      {
        /* File doesn't exist or empty: just write new_content */
        int fd = ir0_open(normalized_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
          int64_t w = ir0_write(fd, new_content, msg_len);
          ir0_close(fd);
        if (w < 0)
        {
          typewriter_vga_print("Error: Could not write to file '", 0x0C);
          typewriter_vga_print(normalized_path, 0x0C);
          typewriter_vga_print("'\n", 0x0C);
        }
        else
        {
          typewriter_vga_print("Written to '", 0x0A);
          typewriter_vga_print(normalized_path, 0x0A);
          typewriter_vga_print("'\n", 0x0A);
        }
        }
      }
    }
    else
    {
      /* Prevent overwriting root or directories */
      if (strcmp(normalized_path, "/") == 0)
      {
        typewriter_vga_print("Error: Refusing to write to root '/'\n", 0x0C);
        kfree(new_content);
        return;
      }
      stat_t st;
      int64_t sret = syscall(SYS_STAT, (uint64_t)normalized_path, (uint64_t)&st, 0);
      if (sret == 0 && S_ISDIR(st.st_mode))
      {
        typewriter_vga_print("Error: Refusing to overwrite a directory\n", 0x0C);
        kfree(new_content);
        return;
      }

      /* Overwrite mode: write new_content directly */
      int fd = ir0_open(normalized_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      int64_t w = (fd >= 0) ? ir0_write(fd, new_content, msg_len) : -1;
      if (fd >= 0)
        ir0_close(fd);
      if (w < 0)
      {
        typewriter_vga_print("Error: Could not write to file '", 0x0C);
        typewriter_vga_print(normalized_path, 0x0C);
        typewriter_vga_print("'\n", 0x0C);
      }
      else
      {
        typewriter_vga_print("Written to '", 0x0A);
        typewriter_vga_print(normalized_path, 0x0A);
        typewriter_vga_print("'\n", 0x0A);
      }
    }

    kfree(new_content);
  }
  else
  {
    /* No redirection, just print to screen */
    shell_write(1, text);
    shell_write(1, "\n");
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

static void cmd_exit(void) { ir0_exit(0); }

static void cmd_netinfo(void)
{
    cmd_cat("/proc/netinfo");
}

static void cmd_arpcache(void)
{
    /* Note: This command runs in user space, so we cannot directly
     * access kernel memory like arp_cache. For now, just show a message.
     * To properly implement this, we would need a syscall that returns
     * the ARP cache entries.
     */
    shell_write(1, "ARP Cache:\n");
    shell_write(1, "==========\n");
    shell_write(1, "  ARP cache information is available via serial output.\n");
    shell_write(1, "  Check serial logs for ARP cache entries.\n");
    shell_write(1, "  (To implement proper display, add a syscall for ARP cache)\n");
}

/* Helper function to perform text substitution */
static char *perform_substitution(const char *original, size_t original_size,
                                  const char *old_str, const char *new_str)
{
  if (!original || !old_str || !new_str)
  {
    return NULL;
  }

  int old_len = 0, new_len = 0;

  /* Calculate string lengths */
  while (old_str[old_len])
    old_len++;
  while (new_str[new_len])
    new_len++;

  if (old_len == 0)
  {
    return NULL; // Can't replace empty string
  }

  /* Count occurrences of old_str in original */
  int count = 0;
  const char *pos = original;
  while (pos && (pos = strstr(pos, old_str)) != NULL)
  {
    count++;
    pos += old_len;
  }

  if (count == 0)
  {
    /* No replacements needed, return copy of original */
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

  /* Calculate new size */
  size_t new_size = original_size + count * (new_len - old_len);
  char *result = (char *)kmalloc(new_size + 1);
  if (!result)
  {
    return NULL;
  }

  /* Perform substitution */
  const char *src = original;
  char *dst = result;

  while (*src)
  {
    /* Check if we found old_str at current position */
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
      /* Copy new_str */
      for (int i = 0; i < new_len; i++)
      {
        *dst++ = new_str[i];
      }
      src += old_len; // Skip old_str
    }
    else
    {
      /* Copy original character */
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

  /* Parse sed command: s/OLD/NEW/ FILE */
  if (!str_starts_with(args, "s/"))
  {
    typewriter_vga_print(
        "Error: Only substitute command 's/OLD/NEW/' supported\n", 0x0C);
    return;
  }

  /* Find the pattern: s/OLD/NEW/ */
  const char *pattern_start = args + 2; // Skip "s/"
  const char *old_end = NULL;
  const char *new_start = NULL;
  const char *new_end = NULL;
  const char *filename = NULL;

  /* Find first '/' (end of OLD) */
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

  /* Find second '/' (end of NEW) */
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

  /* Extract OLD and NEW strings */
  char old_str[256], new_str[256];
  int old_len = old_end - pattern_start;
  int new_len = new_end - new_start;

  if (old_len >= 255 || new_len >= 255)
  {
    typewriter_vga_print("Error: Pattern too long\n", 0x0C);
    return;
  }

  /* Copy strings */
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

  /* Read the file content */
  void *file_data = NULL;
  size_t file_size = 0;

  int fd = ir0_open(filename, O_RDONLY, 0);
  if (fd >= 0)
  {
    stat_t st;
    if (ir0_fstat(fd, &st) >= 0 && st.st_size > 0)
    {
      file_size = (size_t)st.st_size;
      file_data = kmalloc(file_size);
      if (file_data)
      {
        int64_t r = ir0_read(fd, file_data, file_size);
        if (r < 0)
        {
          kfree(file_data);
          file_data = NULL;
          file_size = 0;
        }
      }
    }
    ir0_close(fd);
  }

  int64_t result = (file_data && file_size > 0) ? 0 : -1;

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

  /* Perform text substitution */
  char *original = (char *)file_data;
  char *modified = perform_substitution(original, file_size, old_str, new_str);

  if (!modified)
  {
    typewriter_vga_print("Error: Could not perform substitution\n", 0x0C);
    /* Free the original file data (would need kfree syscall) */
    return;
  }

  /* Write the modified content back to the file */
  fd = ir0_open(filename, O_WRONLY | O_TRUNC, 0);
  if (fd >= 0)
  {
    result = ir0_write(fd, modified, strlen(modified));
    ir0_close(fd);
  }
  else
  {
    result = -1;
  }

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

  /* TODO: Free memory (need kfree syscall or memory management) */
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

/* Copy file: cp SRC DST */
static void cmd_cp(const char *args)
{
  if (!args || *args == '\0')
  {
    shell_write(2, "Usage: cp <src> <dst>\n");
    return;
  }

  /* tokenize */
  char buf[512];
  size_t i = 0;
  while (i < sizeof(buf) - 1 && args[i] && args[i] != '\n')
  {
    buf[i] = args[i];
    i++;
  }
  buf[i] = '\0';
  char *p = buf;
  while (*p == ' ' || *p == '\t')
    p++;
  char *src = p;
  while (*p && *p != ' ' && *p != '\t')
    p++;
  if (*p)
  {
    *p++ = '\0';
  }
  while (*p == ' ' || *p == '\t')
    p++;
  char *dst = p;
  if (!src || !dst || *dst == '\0')
  {
    shell_write(2, "Usage: cp <src> <dst>\n");
    return;
  }

  void *data = NULL;
  size_t size = 0;
  int fd = ir0_open(src, O_RDONLY, 0);
  if (fd >= 0)
  {
    stat_t st;
    if (ir0_fstat(fd, &st) >= 0 && st.st_size > 0)
    {
      size = (size_t)st.st_size;
      data = kmalloc(size);
      if (data)
      {
        int64_t rr = ir0_read(fd, data, size);
        if (rr < 0)
        {
          kfree(data);
          data = NULL;
          size = 0;
        }
      }
    }
    ir0_close(fd);
  }
  int64_t r = (data && size > 0) ? 0 : -1;
  if (r < 0 || !data)
  {
    shell_write(2, "cp: cannot read source\n");
    return;
  }

  fd = ir0_open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  int64_t w = (fd >= 0) ? ir0_write(fd, data, size) : -1;
  if (fd >= 0)
    ir0_close(fd);
  if (w < 0)
  {
    shell_write(2, "cp: cannot write destination\n");
  }
  else
  {
    shell_write(1, "cp: done\n");
  }

  if (data)
    kfree(data);
}

/* Move file: mv SRC DST (implemented as cp + unlink) */
static void cmd_mv(const char *args)
{
  if (!args || *args == '\0')
  {
    shell_write(2, "Usage: mv <src> <dst>\n");
    return;
  }
  
  /* Parse arguments */
  char buf[512];
  size_t i = 0;
  while (i < sizeof(buf) - 1 && args[i] && args[i] != '\n')
  {
    buf[i] = args[i];
    i++;
  }
  buf[i] = '\0';
  
  char *p = buf;
  while (*p == ' ' || *p == '\t')
    p++;
  char *src = p;
  while (*p && *p != ' ' && *p != '\t')
    p++;
  if (*p)
  {
    *p++ = '\0';
  }
  while (*p == ' ' || *p == '\t')
    p++;
  char *dst = p;
  
  if (!src || !dst || *dst == '\0')
  {
    shell_write(2, "Usage: mv <src> <dst>\n");
    return;
  }

  /* Check if destination is a directory */
  stat_t dst_stat;
  char target_path[256];
  int64_t stat_result = syscall(SYS_STAT, (uint64_t)dst, (uint64_t)&dst_stat, 0);
  
  if (stat_result >= 0 && S_ISDIR(dst_stat.st_mode))
  {
    /* Destination is a directory, extract filename from source */
    const char *filename = src;
    const char *last_slash = src;
    for (const char *p = src; *p; p++)
    {
      if (*p == '/')
        last_slash = p + 1;
    }
    if (last_slash != src)
      filename = last_slash;
    
    /* Build target path: dst/filename */
    size_t dst_len = strlen(dst);
    size_t filename_len = strlen(filename);
    if (dst_len + filename_len + 2 > sizeof(target_path))
    {
      shell_write(2, "mv: path too long\n");
      return;
    }
    
    strcpy(target_path, dst);
    if (dst[dst_len - 1] != '/')
    {
      target_path[dst_len] = '/';
      target_path[dst_len + 1] = '\0';
    }
    strcat(target_path, filename);
    dst = target_path;
  }

  /* Copy file content */
  void *data = NULL;
  size_t size = 0;
  int fd = ir0_open(src, O_RDONLY, 0);
  if (fd >= 0)
  {
    stat_t st;
    if (ir0_fstat(fd, &st) >= 0 && st.st_size > 0)
    {
      size = (size_t)st.st_size;
      data = kmalloc(size);
      if (data)
      {
        int64_t rr = ir0_read(fd, data, size);
        if (rr < 0)
        {
          kfree(data);
          data = NULL;
          size = 0;
        }
      }
    }
    ir0_close(fd);
  }
  if (!data)
  {
    shell_write(2, "mv: cannot read source\n");
    return;
  }

  fd = ir0_open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  int64_t w = (fd >= 0) ? ir0_write(fd, data, size) : -1;
  if (fd >= 0)
    ir0_close(fd);
  if (w < 0)
  {
    shell_write(2, "mv: cannot write destination\n");
    if (data)
      kfree(data);
    return;
  }

  /* Unlink source */
  int64_t u = ir0_unlink(src);
  if (u < 0)
  {
    shell_write(2, "mv: copied but failed to remove source\n");
  }
  else
  {
    shell_write(1, "mv: done\n");
  }

  if (data)
    kfree(data);
}

/* ln - create link */
static void cmd_ln(const char *args)
{
  if (!args || *args == '\0')
  {
    shell_write(2, "Usage: ln <oldpath> <newpath>\n");
    return;
  }

  /* Parse arguments: oldpath newpath */
  char buf[256];
  size_t i = 0;
  while (i < sizeof(buf) - 1 && args[i] && args[i] != '\n')
  {
    buf[i] = args[i];
    i++;
  }
  buf[i] = '\0';
  
  char *p = buf;
  while (*p == ' ' || *p == '\t')
    p++;
  char *oldpath = p;
  while (*p && *p != ' ' && *p != '\t')
    p++;
  if (*p)
  {
    *p++ = '\0';
  }
  while (*p == ' ' || *p == '\t')
    p++;
  char *newpath = p;
  
  if (!oldpath || !newpath || *oldpath == '\0' || *newpath == '\0')
  {
    shell_write(2, "Usage: ln <oldpath> <newpath>\n");
    return;
  }

  int64_t result = ir0_link(oldpath, newpath);
  if (result < 0)
  {
    shell_write(2, "ln: failed to create hard link\n");
  }
}

/* chmod MODE PATH - supports both octal (755) and symbolic (+x, u+rw, etc.) */
static void cmd_chmod(const char *args)
{
  if (!args || *args == '\0')
  {
    shell_write(2, "Usage: chmod <mode> <path>\n");
    shell_write(2, "  Octal mode: chmod 755 file\n");
    shell_write(2, "  Symbolic mode: chmod +x file, chmod u+rw file, chmod go-w file\n");
    return;
  }
  
  char buf[256];
  size_t i = 0;
  while (i < sizeof(buf) - 1 && args[i] && args[i] != '\n')
  {
    buf[i] = args[i];
    i++;
  }
  buf[i] = '\0';
  
  char *p = buf;
  while (*p == ' ' || *p == '\t')
    p++;
  char *mode_s = p;
  while (*p && *p != ' ' && *p != '\t')
    p++;
  if (*p)
  {
    *p++ = '\0';
  }
  while (*p == ' ' || *p == '\t')
    p++;
  char *path = p;
  
  if (!mode_s || !path || *path == '\0')
  {
    shell_write(2, "Usage: chmod <mode> <path>\n");
    return;
  }

  int mode = -1;
  
  /* Check if it's octal mode (all digits 0-7) */
  int is_octal = 1;
  for (char *q = mode_s; *q; q++)
  {
    if (*q < '0' || *q > '7')
    {
      is_octal = 0;
      break;
    }
  }
  
  if (is_octal && mode_s[0] != '\0')
  {
    /* Parse octal mode */
    mode = 0;
    for (char *q = mode_s; *q; q++)
    {
      mode = (mode << 3) + (*q - '0');
    }
  }
  else
  {
    /* Parse symbolic mode (e.g., +x, u+rw, go-w) */
    /* First, get current file mode */
    stat_t st;
    int64_t stat_result = syscall(SYS_STAT, (uint64_t)path, (uint64_t)&st, 0);
    if (stat_result < 0)
    {
      shell_write(2, "chmod: cannot access file\n");
      return;
    }
    mode = st.st_mode & 0777;  // Extract permission bits
    
    /* Parse symbolic mode */
    char *s = mode_s;
    int who = 0;  // bitmask: u=1, g=2, o=4, a=7
    int op = 0;   // '+' or '-'
    int perms = 0;  // r=4, w=2, x=1
    
    /* Parse who (u, g, o, a) or default to 'a' */
    while (*s && (*s == 'u' || *s == 'g' || *s == 'o' || *s == 'a'))
    {
      if (*s == 'u') who |= 1;
      else if (*s == 'g') who |= 2;
      else if (*s == 'o') who |= 4;
      else if (*s == 'a') who = 7;
      s++;
    }
    if (who == 0) who = 7;  // default is 'all'
    
    /* Parse operator (+ or -) */
    if (*s == '+' || *s == '-')
    {
      op = *s++;
    }
    else
    {
      shell_write(2, "chmod: invalid symbolic mode (use + or -)\n");
      return;
    }
    
    /* Parse permissions (r, w, x) */
    while (*s && (*s == 'r' || *s == 'w' || *s == 'x'))
    {
      if (*s == 'r') perms |= 4;
      else if (*s == 'w') perms |= 2;
      else if (*s == 'x') perms |= 1;
      s++;
    }
    
    /* Apply changes */
    if (op == '+')
    {
      if (who & 1) mode |= (perms << 6);  // user
      if (who & 2) mode |= (perms << 3);  // group
      if (who & 4) mode |= perms;         // others
    }
    else if (op == '-')
    {
      if (who & 1) mode &= ~(perms << 6);  // user
      if (who & 2) mode &= ~(perms << 3);  // group
      if (who & 4) mode &= ~perms;         // others
    }
  }

  if (mode < 0)
  {
    shell_write(2, "chmod: invalid mode\n");
    return;
  }

  int64_t r = syscall(SYS_CHMOD, (uint64_t)path, (uint64_t)mode, 0);
  if (r < 0)
  {
    shell_write(2, "chmod: failed\n");
  }
}

/* chown not implemented (requires user subsystem) */
static void cmd_chown(const char *args)
{
  (void)args;
  typewriter_vga_print("chown: not implemented (requires user/uid support)\n", 0x0C);
}

/* Mount command: mount <device> <mountpoint> [fstype] */
static void cmd_mount(const char *args)
{
  if (!args || *args == '\0')
  {
    typewriter_vga_print("Usage: mount <device> <mountpoint> [fstype]\n", 0x0C);
    return;
  }

  /* Parse arguments: device mountpoint [fstype] */
  /* copy to local buffer to tokenize safely */
  char buf[256];
  size_t i = 0;
  while (i < sizeof(buf) - 1 && args[i] && args[i] != '\n')
  {
    buf[i] = args[i];
    i++;
  }
  buf[i] = '\0';

  /* find tokens */
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

  /* Robust flag parsing: allow -r, -f, -rf, -fr */
  if (args[0] == '-')
  {
    int i = 1;
    int seen_flag = 0;
    while (args[i] && args[i] != ' ' && args[i] != '\t')
    {
      seen_flag = 1;
      if (args[i] == 'r')
        recursive = 1;
      i++;
    }
    if (seen_flag)
    {
      filename = skip_whitespace(args + i);
    }
    else
    {
      filename = args; // should not happen, fallback
    }
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
    result = ir0_unlink(filename);
    if (result < 0)
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

/* List block devices (lsblk) - shows real disk information */

/* Using itoa from string.h */

/* Touch command: create empty file or update its timestamp */
static void cmd_touch(const char *filename)
{
  if (!filename || *filename == '\0')
  {
    shell_write(2, "Usage: touch FILE\n");
    return;
  }

  /* Use default file permissions (0644 = rw-r--r--) */
  mode_t default_mode = 0644;

  /* Call the filesystem's touch function */
  int64_t result = ir0_touch(filename);
  if (result >= 0)
    ir0_close((int)result);
  if (result < 0)
  {
    vga_print("touch: failed to create/update file\n", 0x0C);
  }
}

/* Helper function to convert number to string */
static void uint64_to_str(uint64_t num, char *str)
{
  char tmp[32];
  char *p = tmp;

  if (num == 0)
  {
    *p++ = '0';
  }
  else
  {
    while (num > 0)
    {
      *p++ = '0' + (num % 10);
      num /= 10;
    }
  }

  /* Reverse the string */
  while (p > tmp)
  {
    *str++ = *--p;
  }
  *str = '\0';
}

static void cmd_lsblk(const char *args)
{
  (void)args; // Unused parameter

  /* Print header */
  typewriter_vga_print("NAME        MAJ:MIN   SIZE (bytes)    MODEL\n", 0x0F);
  typewriter_vga_print("------------------------------------------------\n", 0x07);

  /* Check each possible ATA device (0-3) */
  for (uint8_t i = 0; i < 4; i++)
  {
    ata_device_info_t info;

    /* Get device info */
    if (!ata_get_device_info(i, &info))
    {
      continue; // Skip if device not present
    }

    char name[8];

    /* Generate device name (hda, hdb, etc.) */
    name[0] = 'h';
    name[1] = 'd' + i;
    name[2] = '\0';

    /* Print device name */
    typewriter_vga_print(name, 0x0F);
    typewriter_vga_print("         ", 0x0F);

    /* Print major:minor */
    char num_buf[16];
    itoa(i, num_buf, 10);
    typewriter_vga_print(num_buf, 0x0F);
    typewriter_vga_print(":0", 0x0F);
    typewriter_vga_print("       ", 0x0F);

    /* Print size in bytes */
    char size_buf[32];
    uint64_to_str(info.capacity_bytes, size_buf);
    typewriter_vga_print(size_buf, 0x0A);

    /* Add some padding for alignment */
    int pad = 15 - strlen(size_buf);
    while (pad-- > 0)
    {
      typewriter_vga_print(" ", 0x0A);
    }

    /* Print model if available */
    if (info.model[0] != '\0')
    {
      typewriter_vga_print("  ", 0x0F);
      typewriter_vga_print(info.model, 0x0F);
    }

    typewriter_vga_print("\n", 0x0F);
  }
}

static void cmd_df(const char *args __attribute__((unused)))
{
  syscall(95, 0, 0, 0);
}
static void cmd_lsdrv(const char *args __attribute__((unused)))
{
  cmd_cat("/proc/drivers");
}

static void cmd_dmesg(const char *args __attribute__((unused)))
{
  cmd_cat("/dev/kmsg");
}

static void cmd_audio_test(const char *args __attribute__((unused)))
{
  syscall(112, 0, 0, 0);
}

static void cmd_mouse_test(const char *args __attribute__((unused)))
{
  syscall(113, 0, 0, 0);
}

/* Parse IP address from string (e.g., "192.168.1.1") */
static ip4_addr_t parse_ip(const char *ip_str)
{
    if (!ip_str || *ip_str == '\0')
        return 0;
    
    uint8_t octets[4] = {0, 0, 0, 0};
    int octet_idx = 0;
    int value = 0;
    const char *p = ip_str;
    
    while (*p && octet_idx < 4)
    {
        if (*p >= '0' && *p <= '9')
        {
            value = value * 10 + (*p - '0');
            if (value > 255)
                return 0;  /* Invalid */
        }
        else if (*p == '.')
        {
            if (octet_idx >= 4)
                return 0;
            octets[octet_idx++] = (uint8_t)value;
            value = 0;
        }
        else
        {
            return 0;  /* Invalid character */
        }
        p++;
    }
    
    if (octet_idx < 3)
        return 0;  /* Not enough octets */
    
    octets[octet_idx] = (uint8_t)value;
    
    /* Convert to network byte order */
    return htonl((octets[0] << 24) | (octets[1] << 16) | (octets[2] << 8) | octets[3]);
}

static void cmd_ping(const char *args)
{
    if (!args || *args == '\0')
    {
        shell_write(2, "Usage: ping <IP_ADDRESS>\n");
        shell_write(2, "Example: ping 192.168.1.1\n");
        return;
    }
    
    ip4_addr_t dest_ip = parse_ip(args);
    if (dest_ip == 0)
    {
        shell_write(2, "Invalid IP address format. Use: XXX.XXX.XXX.XXX\n");
        return;
    }
    
    int64_t ret = ir0_ping(args);
    if (ret != 0)
    {
        shell_write(2, "Ping failed\n");
    }
}

static void cmd_ifconfig(const char *args)
{
    if (!args || *args == '\0')
    {
        /* Show current configuration */
        ir0_ifconfig("");
        return;
    }
    
    /* Parse arguments: ifconfig <ip> [netmask] [gateway] */
    char arg_copy[256];
    size_t i = 0;
    const char *p = args;
    while (i < sizeof(arg_copy) - 1 && *p && *p != '\n')
        arg_copy[i++] = *p++;
    arg_copy[i] = '\0';
    
    /* Find first space (IP address) */
    char *ip_str = arg_copy;
    char *netmask_str = NULL;
    char *gateway_str = NULL;
    
    char *q = ip_str;
    while (*q && *q != ' ' && *q != '\t')
        q++;
    if (*q)
    {
        *q++ = '\0';
        netmask_str = (char *)skip_whitespace(q);
        q = netmask_str;
        while (*q && *q != ' ' && *q != '\t')
            q++;
        if (*q)
        {
            *q++ = '\0';
            gateway_str = (char *)skip_whitespace(q);
        }
    }
    
    ip4_addr_t ip = parse_ip(ip_str);
    ip4_addr_t netmask = netmask_str ? parse_ip(netmask_str) : 0;
    ip4_addr_t gateway = gateway_str ? parse_ip(gateway_str) : 0;
    
    if (ip == 0 && ip_str[0] != '\0')
    {
        shell_write(2, "Invalid IP address format\n");
        return;
    }
    
    char config[256];
snprintf(config, sizeof(config), "%s %s %s", ip_str, netmask_str, gateway_str);
ir0_ifconfig(config);
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
    {"whoami", (void (*)(const char *))cmd_whoami, "whoami", "Print effective user name"},
    {"ls", cmd_ls, "ls [-l] [DIR]", "List directory"},
    {"lsblk", cmd_lsblk, "lsblk", "List block devices"},
    {"df", (void (*)(const char *))cmd_df, "df", "Show disk space"},
    {"cp", cmd_cp, "cp SRC DST", "Copy file"},
    {"mv", cmd_mv, "mv SRC DST", "Move (rename) file"},
    {"ln", (void (*)(const char *))cmd_ln, "ln OLDPATH NEWPATH", "Create hard link"},
    {"cat", cmd_cat, "cat FILE", "Print file"},
    {"mkdir", cmd_mkdir, "mkdir DIR", "Create directory"},
    {"rmdir", cmd_rmdir, "rmdir [-e] DIR", "Remove directory (use -e to force empty dir)"},
    {"rm", cmd_rm, "rm [-r] FILE", "Remove file or dir"},
    {"cd", cmd_cd, "cd [DIR]", "Change directory"},
    {"pwd", (void (*)(const char *))cmd_pwd, "pwd", "Print working directory"},
    {"ps", (void (*)(const char *))cmd_ps, "ps", "List processes"},
    {"echo", cmd_echo, "echo TEXT", "Print text or write to file"},
    {"exec", cmd_exec, "exec FILE", "Execute binary"},
    {"sed", cmd_sed, "sed 's/OLD/NEW/' FILE", "Substitute text in file"},
    {"type", cmd_type, "type [mode]", "Typewriter effect control"},
    {"mount", cmd_mount, "mount DEV MOUNTPOINT [fstype]", "Mount filesystem"},
    {"chmod", (void (*)(const char *))cmd_chmod, "chmod MODE PATH", "Change file mode"},
    {"chown", (void (*)(const char *))cmd_chown, "chown USER PATH", "Change file owner (not implemented)"},
    {"lsdrv", cmd_lsdrv, "lsdrv", "List all registered drivers"},
    {"dmesg", cmd_dmesg, "dmesg", "Show kernel log buffer (like dmesg/journalctl)"},
    {"audio_test", cmd_audio_test, "audio_test", "Test Sound Blaster audio"},
    {"mouse_test", cmd_mouse_test, "mouse_test", "Test PS/2 mouse"},
    {"exit", (void (*)(const char *))cmd_exit, "exit", "Exit shell"},
    {"touch", cmd_touch, "touch FILE", "Create empty file or update timestamp"},
    {"netinfo", (void (*)(const char *))cmd_netinfo, "netinfo", "Display network interface information"},
    {"arpcache", (void (*)(const char *))cmd_arpcache, "arpcache", "Display ARP cache"},
    {"ping", cmd_ping, "ping <IP>", "Send ICMP Echo Request (ping) to IP address"},
    {"ifconfig", cmd_ifconfig, "ifconfig [IP] [NETMASK] [GATEWAY]", "Configure or display network interface"},
};

static void cmd_list_help(void)
{
  for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
  {
    shell_write(1, "  ");
    shell_write(1, commands[i].usage);
    shell_write(1, " - ");
    shell_write(1, commands[i].desc);
    shell_write(1, "\n");
  }
}

static void execute_single_command(const char *cmd_line)
{
  char buf[256];
  size_t i = 0;
  const char *p = skip_whitespace(cmd_line);
  if (*p == '\0')
    return;
  while (i < sizeof(buf) - 1 && *p && *p != '\n' && *p != '|')
    buf[i++] = *p++;
  buf[i] = '\0';

  char *cmd_name = buf;
  while (*cmd_name && *cmd_name != ' ' && *cmd_name != '\t')
    cmd_name++;
  const char *rest = NULL;
  if (*cmd_name)
  {
    *cmd_name++ = '\0';
    rest = skip_whitespace(cmd_name);
  }

  for (size_t j = 0; j < sizeof(commands) / sizeof(commands[0]); j++)
  {
    if (strcmp(buf, commands[j].name) == 0)
    {
      commands[j].handler(rest);
      return;
    }
  }

  shell_write(2, "Unknown command: ");
  shell_write(2, buf);
  shell_write(2, "\nType 'help' for available commands\n");
}

static void execute_command(const char *cmd)
{
  if (!cmd || *cmd == '\0')
    return;

  char cmd_copy[512];
  size_t len = 0;
  const char *src = cmd;
  while (len < sizeof(cmd_copy) - 1 && *src && *src != '\n')
    cmd_copy[len++] = *src++;
  cmd_copy[len] = '\0';

  char *pipe_pos = strchr(cmd_copy, '|');
  if (!pipe_pos)
  {
    execute_single_command(cmd_copy);
    return;
  }

  *pipe_pos = '\0';
  char *first_cmd = cmd_copy;
  char *second_cmd = pipe_pos + 1;

  first_cmd = (char *)skip_whitespace(first_cmd);
  second_cmd = (char *)skip_whitespace(second_cmd);

  if (*first_cmd == '\0' || *second_cmd == '\0')
  {
    shell_write(2, "Invalid pipe syntax\n");
    return;
  }

  execute_single_command(first_cmd);
  execute_single_command(second_cmd);
}

void shell_entry(void)
{
  char input[256];
  int input_pos = 0;

  vga_clear();
  vga_print("IR0 DebShell v0.0.1 pre-release 1\n", 0x0B);
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
