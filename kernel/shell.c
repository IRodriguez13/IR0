// shell.c - Shell running in Ring 3 (user space)
#include <stddef.h>
#include <stdint.h>

// Syscall numbers (simplified for our kernel)
#define SYS_EXIT 0
#define SYS_WRITE 1
#define SYS_READ 2
#define SYS_GETPID 3
#define SYS_LS 5
#define SYS_MKDIR 6
#define SYS_PS 7
#define SYS_CAT 9
#define SYS_TOUCH 10
#define SYS_RM 11
#define SYS_RMDIR 40
#define SYS_FORK 12
#define SYS_WAITPID 13

// Syscall wrapper - uses int 0x80 - This is a ugly way to call syscalls, but i promise i'll turn it more elegant xd
static inline int64_t syscall(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3)
{
  int64_t sysret; 
  __asm__ volatile("mov %1, %%rax\n"
                   "mov %2, %%rbx\n"
                   "mov %3, %%rcx\n"
                   "mov %4, %%rdx\n"
                   "int $0x80\n"
                   "mov %%rax, %0\n"
                   : "=r"(sysret)
                   : "r"(num), "r"(arg1), "r"(arg2), "r"(arg3)
                   : "rax", "rbx", "rcx", "rdx", "memory");
  return sysret;
}

// Syscall wrappers
static inline int64_t sys_read(int fd, void *buf, size_t count)
{
  return syscall(SYS_READ, fd, (uint64_t)buf, count);
}

static inline int64_t sys_getpid(void) { return syscall(SYS_GETPID, 0, 0, 0); }

// Framebuffer management
static int cursor_pos = 0;
static int screen_width = 80;
static int screen_height = 25;

// VGA text mode functions (better than framebuffer for now)
static void fb_putchar(char c, uint8_t color)
{
  volatile uint16_t *vga = (volatile uint16_t *)0xB8000;

  if (c == '\n')
  {
    cursor_pos = (cursor_pos / screen_width + 1) * screen_width;
    if (cursor_pos >= screen_width * screen_height)
    {
      // Scroll screen
      for (int i = 0; i < 24 * 80; i++)
      {
        vga[i] = vga[i + 80];
      }
      for (int i = 24 * 80; i < 25 * 80; i++)
      {
        vga[i] = 0x0F20;
      }
      cursor_pos = 24 * 80;
    }
  }
  else if (c == '\b')
  {
    if (cursor_pos > 0)
    {
      cursor_pos--;
      vga[cursor_pos] = (color << 8) | ' ';
    }
  }
  else
  {
    vga[cursor_pos] = (color << 8) | c;
    cursor_pos++;
    if (cursor_pos >= screen_width * screen_height)
    {
      // Scroll screen
      for (int i = 0; i < 24 * 80; i++)
      {
        vga[i] = vga[i + 80];
      }
      for (int i = 24 * 80; i < 25 * 80; i++)
      {
        vga[i] = 0x0F20;
      }
      cursor_pos = 24 * 80;
    }
  }
}

// Write string to framebuffer
static void fb_print(const char *str, uint8_t color)
{
  for (int i = 0; str[i] != '\0'; i++)
  {
    fb_putchar(str[i], color);
  }
}

// Simple string comparison
static int str_starts_with(const char *str, const char *prefix)
{
  while (*prefix)
  {
    if (*str != *prefix)
      return 0;
    str++;
    prefix++;
  }
  return 1;
}

// Find argument after command
static const char *find_arg(const char *cmd, const char *command)
{
  int cmd_len = 0;
  while (command[cmd_len])
    cmd_len++; // strlen

  if (!str_starts_with(cmd, command))
    return 0;

  const char *arg = cmd + cmd_len;
  while (*arg == ' ')
    arg++; // skip spaces

  return (*arg != '\0') ? arg : 0;
}

// Process commands
static void process_command(const char *cmd)
{
  if (cmd[0] == '\0')
    return;

  if (str_starts_with(cmd, "ls"))
  {
    const char *path = find_arg(cmd, "ls");
    syscall(SYS_LS, (uint64_t)(path ? path : "/"), 0, 0);
  }
  else if (str_starts_with(cmd, "ps"))
  {
    syscall(SYS_PS, 0, 0, 0);
  }
  else if (str_starts_with(cmd, "cat"))
  {
    const char *path = find_arg(cmd, "cat");
    if (path)
    {
      syscall(SYS_CAT, (uint64_t)path, 0, 0);
    }
    else
    {
      fb_print("Usage: cat <filename>\n", 0x0C);
    }
  }
  else if (str_starts_with(cmd, "touch"))
  {
    const char *path = find_arg(cmd, "touch");
    if (path)
    {
      syscall(SYS_TOUCH, (uint64_t)path, 0, 0);
    }
    else
    {
      fb_print("Usage: touch <filename>\n", 0x0C);
    }
  }
  else if (str_starts_with(cmd, "rmdir"))
  {
    const char *path = find_arg(cmd, "rmdir");
    if (path)
    {
      syscall(SYS_RMDIR, (uint64_t)path, 0, 0);
    }
    else
    {
      fb_print("Usage: rmdir <directory>\n", 0x0C);
    }
  }
  else if (str_starts_with(cmd, "rm"))
  {
    const char *path = find_arg(cmd, "rm");
    if (path)
    {
      syscall(SYS_RM, (uint64_t)path, 0, 0);
    }
    else
    {
      fb_print("Usage: rm <filename>\n", 0x0C);
    }
  }
  else if (str_starts_with(cmd, "mkdir"))
  {
    const char *path = find_arg(cmd, "mkdir");
    if (path)
    {
      syscall(SYS_MKDIR, (uint64_t)path, 0755, 0);
    }
    else
    {
      fb_print("Usage: mkdir <directory>\n", 0x0C);
    }
  }
  else if (str_starts_with(cmd, "fork"))
  {
    fb_print("Testing fork()...\n", 0x0E);
    int64_t pid = syscall(SYS_FORK, 0, 0, 0);
    if (pid == 0)
    {
      // Child process
      fb_print("Child: Hello from child process!\n", 0x0A);
      fb_print("Child: My PID is ", 0x0A);
      int64_t child_pid = syscall(SYS_GETPID, 0, 0, 0);
      if (child_pid >= 0 && child_pid <= 99)
      {
        char pid_str[3];
        if (child_pid >= 10)
        {
          pid_str[0] = '0' + (child_pid / 10);
          pid_str[1] = '0' + (child_pid % 10);
          pid_str[2] = '\0';
        }
        else
        {
          pid_str[0] = '0' + child_pid;
          pid_str[1] = '\0';
        }
        fb_print(pid_str, 0x0A);
      }
      fb_print("\n", 0x0A);
      syscall(SYS_EXIT, 42, 0, 0); // Child exits with code 42
    }
    else if (pid > 0)
    {
      // Parent process
      fb_print("Parent: Child PID is ", 0x0C);
      if (pid >= 0 && pid <= 99)
      {
        char pid_str[3];
        if (pid >= 10)
        {
          pid_str[0] = '0' + (pid / 10);
          pid_str[1] = '0' + (pid % 10);
          pid_str[2] = '\0';
        }
        else
        {
          pid_str[0] = '0' + pid;
          pid_str[1] = '\0';
        }
        fb_print(pid_str, 0x0C);
      }
      fb_print("\n", 0x0C);

      // Wait for child
      int status;
      int64_t waited_pid = syscall(SYS_WAITPID, pid, (uint64_t)&status, 0);
      if (waited_pid > 0)
      {
        fb_print("Parent: Child exited with status ", 0x0C);
        if (status >= 0 && status <= 99)
        {
          char status_str[3];
          if (status >= 10)
          {
            status_str[0] = '0' + (status / 10);
            status_str[1] = '0' + (status % 10);
            status_str[2] = '\0';
          }
          else
          {
            status_str[0] = '0' + status;
            status_str[1] = '\0';
          }
          fb_print(status_str, 0x0C);
        }
        fb_print("\n", 0x0C);
      }
      else
      {
        fb_print("Parent: Wait failed\n", 0x0C);
      }
    }
    else
    {
      fb_print("Fork failed!\n", 0x0C);
    }
  }
  else if (str_starts_with(cmd, "clear"))
  {
    // Clear screen by resetting cursor and filling with spaces
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    for (int i = 0; i < 80 * 25; i++)
    {
      vga[i] = 0x0F20; // White on black, space
    }
    cursor_pos = 0;
  }
  else if (str_starts_with(cmd, "help"))
  {
    fb_print("Available commands:\n", 0x0E);
    fb_print("  ls [path]       - List files\n", 0x0F);
    fb_print("  cat <file>      - Show file contents\n", 0x0F);
    fb_print("  touch <file>    - Create empty file\n", 0x0F);
    fb_print("  rm <file>       - Remove file\n", 0x0F);
    fb_print("  mkdir <dir>     - Create directory\n", 0x0F);
    fb_print("  rmdir <dir>     - Remove directory\n", 0x0F);
    fb_print("  ps              - Show processes\n", 0x0F);
    fb_print("  fork            - Test fork() syscall\n", 0x0F);
    fb_print("  clear           - Clear screen\n", 0x0F);
    fb_print("  help            - Show this help\n", 0x0F);
    fb_print("  exit            - Exit shell\n", 0x0F);
  }
  else if (str_starts_with(cmd, "exit"))
  {
    fb_print("Exiting shell...\n", 0x0C);
    syscall(SYS_EXIT, 0, 0, 0);
  }
  else
  {
    fb_print("Unknown command: ", 0x0C);
    fb_print(cmd, 0x0F);
    fb_print("\nType 'help' for available commands\n", 0x0E);
  }
}

// Shell entry point - runs in Ring 3
void shell_ring3_entry(void)
{
  cursor_pos = 0;

  // Clear screen first
  volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
  for (int i = 0; i < 80 * 25; i++)
  {
    vga[i] = 0x0F20; // White on black, space
  }
  cursor_pos = 0;

  // Show minimal banner (Linux 0.0.1 style)
  int64_t pid = sys_getpid();

  // Show banner
  fb_print("                === IR0 SHELL ===\n\n", 0x0F);
  // Test getpid syscall
  fb_print("initproc1 Process PID: ", 0x0E);
  if (pid >= 0 && pid <= 9)
  {
    char pid_str[2] = {'0' + (char)pid, '\0'};
    fb_print(pid_str, 0x0F);
  }
  else
  {
    fb_print("ERROR", 0x0C);
  }
  fb_print("\n", 0x0F);

  fb_print("Type 'help' for commands, ESC to exit\n", 0x0B);

  char buffer[64];
  int pos = 0;
  int echo_pos = 0; // Position for visual echo

  fb_print("shell> ", 0x0E);
  echo_pos = cursor_pos; // Remember where input starts

  // Main input loop
  while (1)
  {
    char c;
    int64_t bytes_read = sys_read(0, &c, 1); // STDIN

    if (bytes_read > 0)
    {
      if (c == '\n' || c == '\r')
      {
        // Enter - process command
        fb_putchar('\n', 0x0F);
        buffer[pos] = '\0';

        if (pos > 0)
        {
          process_command(buffer);
        }

        // Reset for next command
        pos = 0;
        fb_print("shell> ", 0x0E);
        echo_pos = cursor_pos;
      }
      else if (c == '\b' || c == 127)
      {
        // Backspace - manejar visualmente
        if (pos > 0)
        {
          pos--;
          buffer[pos] = '\0';

          // Borrar visualmente: mover cursor atrás y escribir espacio
          if (cursor_pos > echo_pos)
          {
            cursor_pos--;
            volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
            vga[cursor_pos] = 0x0F20; // Espacio blanco
          }
        }
      }
      else if (c == 27)
      {
        // ESC - exit
        fb_print("\nExiting...\n", 0x0C);
        syscall(SYS_EXIT, 0, 0, 0);
      }
      else if (c >= 32 && c < 127 && pos < 63)
      {
        // Regular character
        buffer[pos++] = c;
        fb_putchar(c, 0x0F);
      }
    }
    else
    {
      // No input, small delay
      for (volatile int i = 0; i < 5000; i++);
    }
  }
}