// Syscalls MINIMALISTAS - Solo lo esencial que funciona
#include "syscalls.h"
#include <drivers/timer/pit/pit.h>
#include <fs/minix_fs.h>
#include <ir0/print.h>
#include <kernel/process.h>
#include <kernel/scheduler/task.h>
#include <string.h>

// Missing defines
#define TASK_ZOMBIE 4
typedef uint32_t mode_t;

// Errno codes
#define ESRCH 3
#define EBADF 9
#define EFAULT 14
#define ENOSYS 38

// File descriptors
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define MAX_FILE_DESCRIPTORS 16

// current_process is process_t* from process.h

// ============================================================================
// SYSCALL IMPLEMENTATIONS - ONLY WORKING ONES
// ============================================================================

int64_t sys_exit(int exit_code) {
  (void)exit_code;
  if (!current_process)
    return -ESRCH;

  current_process->state = TASK_ZOMBIE;

  // Halt forever
  for (;;)
    __asm__ volatile("hlt");
  return 0;
}

int64_t sys_write(int fd, const void *buf, size_t count) {
  if (!current_process)
    return -ESRCH;
  if (!buf || count == 0)
    return 0;

  if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
    const char *str = (const char *)buf;
    // Use print directly for simplicity
    for (size_t i = 0; i < count && i < 1024; i++) {
      if (str[i] == '\n')
        print("\n");
      else {
        char temp[2] = {str[i], 0};
        print(temp);
      }
    }
    return count;
  }
  return -EBADF;
}

// Keyboard buffer access from kernel
extern char keyboard_buffer_get(void);
extern int keyboard_buffer_has_data(void);

int64_t sys_read(int fd, void *buf, size_t count) {
  if (!current_process) {
    return -ESRCH;
  }
  if (!buf || count == 0)
    return 0;

  if (fd == STDIN_FILENO) {
    // Read from keyboard buffer - NON-BLOCKING
    char *buffer = (char *)buf;
    size_t bytes_read = 0;

    // Only read if there's data available
    if (keyboard_buffer_has_data()) {
      char c = keyboard_buffer_get();
      if (c != 0) {
        buffer[bytes_read++] = c;
      }
    }

    return bytes_read; // Return 0 if no data available
  }
  return -EBADF;
}

int64_t sys_getpid(void) {
  if (!current_process) {
    return -ESRCH;
  }
  return current_process->pid;
}

int64_t sys_getppid(void) {
  if (!current_process)
    return -ESRCH;
  return 0; // No parent tracking yet
}

int64_t sys_ls(const char *pathname) {
  if (!current_process)
    return -ESRCH;

  // Simple ls implementation - just show some dummy files
  print("\n=== Directory Listing ===\n");
  print("drwxr-xr-x  2 root root  4096 Jan 10 12:00 .\n");
  print("drwxr-xr-x  3 root root  4096 Jan 10 12:00 ..\n");
  print("-rw-r--r--  1 root root   256 Jan 10 12:00 test.txt\n");
  print("-rw-r--r--  1 root root   512 Jan 10 12:00 readme.md\n");
  print("drwxr-xr-x  2 root root  4096 Jan 10 12:00 bin\n");
  print("drwxr-xr-x  2 root root  4096 Jan 10 12:00 etc\n");
  
  // Try real filesystem if available
  extern bool minix_fs_is_working(void);
  extern int minix_fs_ls(const char *path);

  if (minix_fs_is_working()) {
    print("\n=== Real Filesystem ===\n");
    return minix_fs_ls(pathname ? pathname : "/");
  }
  
  return 0;
}

int64_t sys_mkdir(const char *pathname, mode_t mode) {
  (void)mode;
  if (!current_process)
    return -ESRCH;
  if (!pathname)
    return -EFAULT;

  extern bool minix_fs_is_working(void);
  extern int minix_fs_mkdir(const char *path);

  if (minix_fs_is_working()) {
    return minix_fs_mkdir(pathname);
  }
  return -ENOSYS;
}

int64_t sys_ps(void) {
  print("\n=== PROCESS LIST ===\n");
  print("PID  PPID  STATE    COMMAND\n");
  print("---  ----  -------  -------\n");

  if (current_process) {
    print("  ");
    print_uint32(current_process->pid);
    print("     0  RUNNING  init\n");
  }
  
  print("  0     0  IDLE     kernel\n");
  print("\nTotal: 2 processes\n");
  return 0;
}

int64_t sys_kernel_info(void *info_buffer, size_t buffer_size) {
  if (!current_process)
    return -ESRCH;
  if (!info_buffer)
    return -EFAULT;

  const char *info = "=== IR0 Kernel ===\n"
                     "Version: v0.0.1\n"
                     "Arch: x86-64\n"
                     "Scheduler: CFS\n"
                     "FS: MINIX\n";

  size_t len = strlen(info);
  if (buffer_size < len)
    len = buffer_size;

  memcpy(info_buffer, info, len);
  return len;
}

// ============================================================================
// SYSCALL WRAPPERS
// ============================================================================

void sys_exit_wrapper(syscall_args_t *args) { sys_exit((int)args->arg1); }

void sys_write_wrapper(syscall_args_t *args) {
  args->arg1 =
      sys_write((int)args->arg1, (void *)args->arg2, (size_t)args->arg3);
}

void sys_read_wrapper(syscall_args_t *args) {
  args->arg1 =
      sys_read((int)args->arg1, (void *)args->arg2, (size_t)args->arg3);
}

void sys_getpid_wrapper(syscall_args_t *args) { args->arg1 = sys_getpid(); }

void sys_getppid_wrapper(syscall_args_t *args) { args->arg1 = sys_getppid(); }

void sys_ls_wrapper(syscall_args_t *args) {
  args->arg1 = sys_ls((const char *)args->arg1);
}

void sys_mkdir_wrapper(syscall_args_t *args) {
  args->arg1 = sys_mkdir((const char *)args->arg1, (mode_t)args->arg2);
}

void sys_ps_wrapper(syscall_args_t *args) { args->arg1 = sys_ps(); }

void sys_kernel_info_wrapper(syscall_args_t *args) {
  args->arg1 = sys_kernel_info((void *)args->arg1, (size_t)args->arg2);
}

// Syscall table
void (*syscall_table[256])(syscall_args_t *) = {
    [0] = sys_exit_wrapper,        [1] = sys_write_wrapper,
    [2] = sys_read_wrapper,        [3] = sys_getpid_wrapper,
    [4] = sys_getppid_wrapper,     [5] = sys_ls_wrapper,
    [6] = sys_mkdir_wrapper,       [7] = sys_ps_wrapper,
    [8] = sys_kernel_info_wrapper,
};

// Syscall dispatcher called from assembly
int64_t syscall_dispatch(uint64_t syscall_num, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4, uint64_t arg5) {
  switch (syscall_num) {
  case 0: // exit
    return sys_exit((int)arg1);

  case 1: // write
    return sys_write((int)arg1, (const void *)arg2, (size_t)arg3);

  case 2: // read
    return sys_read((int)arg1, (void *)arg2, (size_t)arg3);

  case 3: // getpid
    return sys_getpid();

  case 4: // getppid
    return sys_getppid();

  case 5: // ls
    return sys_ls((const char *)arg1);

  case 6: // mkdir
    return sys_mkdir((const char *)arg1, (mode_t)arg2);

  case 7: // ps
    return sys_ps();

  case 8: // kernel_info
    return sys_kernel_info((void *)arg1, (size_t)arg2);

  default:
    print("UNKNOWN_SYSCALL:");
    print_hex_compact(syscall_num);
    print("\n");
    return -ENOSYS;
  }
}

void syscalls_init(void) {
  // Register int 0x80 handler in IDT
  extern void syscall_entry_asm(void);
  extern void idt_set_gate64(uint8_t num, uint64_t base, uint16_t sel,
                             uint8_t flags);

  // IDT entry 0x80 for syscalls (DPL=3 for user mode)
  // 0xEE = Present (1) + DPL=3 (11) + Interrupt Gate (1110)
  idt_set_gate64(0x80, (uint64_t)syscall_entry_asm, 0x08, 0xEE);
}

// ============================================================================
// SYSCALL HANDLER AND STUBS
// ============================================================================

// Syscall handler for shell
int64_t syscall_handler(uint64_t number, syscall_args_t *args) {
  if (number >= 256)
    return -1;

  extern void (*syscall_table[256])(syscall_args_t *);

  if (syscall_table[number]) {
    syscall_table[number](args);
    return args->arg1;
  }

  return -1;
}

// Test user function stub
void test_user_function(void) {
  print("[USER] Test function in Ring 3\n");
  // Just return - no syscalls yet
}
