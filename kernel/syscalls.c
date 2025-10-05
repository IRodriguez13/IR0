// Syscalls - Essential system calls only
#include "syscalls.h"
#include "../drivers/serial/serial.h"
#include "../includes/ir0/print.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Forward declarations
// Removed current_process extern - using local current_proc
extern char keyboard_buffer_get(void);
extern int keyboard_buffer_has_data(void);
extern bool minix_fs_is_working(void);
extern int minix_fs_ls(const char *path);
extern int minix_fs_mkdir(const char *path);
extern int minix_fs_cat(const char *path);
extern int minix_fs_touch(const char *path);
extern int minix_fs_rm(const char *path);
extern int minix_fs_rmdir(const char *path);
extern int process_fork(void);
extern int process_wait(int pid, int *status);
extern void print_hex_compact(uint32_t num);

// Basic types and constants
typedef uint32_t mode_t;
typedef int32_t pid_t;
typedef long intptr_t;
typedef long off_t;

// Process states
#define TASK_ZOMBIE 4

// Simple process structure for syscalls
struct simple_process {
  pid_t pid;
  int state;
  void *heap_start; // Start of process heap
  void *heap_end;   // Current break (end of heap)
  void *heap_limit; // Maximum heap size
};

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

// Global current process pointer
static struct simple_process *current_proc = NULL;

// ============================================================================
// SYSCALL IMPLEMENTATIONS - ONLY WORKING ONES
// ============================================================================

int64_t sys_exit(int exit_code) {
  (void)exit_code;
  if (!current_proc)
    return -ESRCH;

  current_proc->state = TASK_ZOMBIE;

  // Halt forever
  for (;;)
    __asm__ volatile("hlt");
  return 0;
}

int64_t sys_write(int fd, const void *buf, size_t count) {
  if (!current_proc)
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
    return (int64_t)count;
  }
  return -EBADF;
}

// Remove duplicate declarations

int64_t sys_read(int fd, void *buf, size_t count) {
  if (!current_proc)
    return -ESRCH;
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

    return (int64_t)bytes_read; // Return 0 if no data available
  }
  return -EBADF;
}

int64_t sys_getpid(void) {
  if (!current_proc)
    return -ESRCH;
  return current_proc->pid;
}

int64_t sys_getppid(void) {
  if (!current_proc)
    return -ESRCH;
  return 0; // No parent tracking yet
}

int64_t sys_ls(const char *pathname) {
  if (!current_proc)
    return -ESRCH;

  // Use MINIX filesystem directly
  const char *target_path = pathname ? pathname : "/";
  return minix_fs_ls(target_path);
}

int64_t sys_mkdir(const char *pathname, mode_t mode) {
  (void)mode;
  if (!current_proc)
    return -ESRCH;
  if (!pathname)
    return -EFAULT;

  if (minix_fs_is_working())
    return minix_fs_mkdir(pathname);
  return -ENOSYS;
}

// Helper function to convert number to string
static void int_to_str(int num, char *str, int *len) {
  *len = 0;
  if (num == 0) {
    str[(*len)++] = '0';
  } else {
    int temp = num;
    int digits = 0;
    while (temp > 0) {
      temp /= 10;
      digits++;
    }
    for (int i = digits - 1; i >= 0; i--) {
      str[i] = '0' + (num % 10);
      num /= 10;
    }
    *len = digits;
  }
  str[*len] = '\0';
}

int64_t sys_ps(void) {
  // Access real process functions
  extern void *get_process_list(void);
  extern void process_print_all(void);
  extern void *current_process;

  sys_write(1, "PID  STATE    COMMAND\n", 21);
  sys_write(1, "---  -------  -------\n", 22);

  // Debug: print process list to serial
  serial_print("SERIAL: sys_ps called, dumping process list:\n");

  // Check both process systems
  serial_print("SERIAL: Simple current_proc = ");
  if (current_proc) {
    serial_print_hex32(current_proc->pid);
  } else {
    serial_print("NULL");
  }
  serial_print("\n");

  serial_print("SERIAL: Real current_process = ");
  serial_print_hex32((uint32_t)(uintptr_t)current_process);
  serial_print("\n");

  process_print_all();

  // Show kernel process (always PID 0)
  sys_write(1, "  0  IDLE     kernel\n", 20);

  // Show simple process (PID 1)
  if (current_proc) {
    sys_write(1, "  1  RUNNING  shell\n", 19);
  }

  // Get real process list
  void *proc_list = get_process_list();

  if (proc_list) {
    serial_print("SERIAL: Found process list, showing processes\n");

    // Use external function to iterate safely
    extern void show_process_list_in_shell(void);
    show_process_list_in_shell();
  } else {
    serial_print("SERIAL: No process list found\n");

    // Fallback: show current process only
    if (current_proc) {
      sys_write(1, "  1  RUNNING  shell\n", 19);
    } else {
      sys_write(1, "  No processes found\n", 21);
    }
  }

  return 0;
}

int64_t sys_cat(const char *pathname) {
  if (!current_proc || !pathname)
    return -EFAULT;

  if (minix_fs_is_working())
    return minix_fs_cat(pathname);

  sys_write(STDERR_FILENO, "Error: filesystem not ready\n", 29);
  return -1;
}

int64_t sys_touch(const char *pathname) {
  if (!current_proc || !pathname)
    return -EFAULT;

  if (minix_fs_is_working())
    return minix_fs_touch(pathname);

  sys_write(STDERR_FILENO, "Error: filesystem not ready\n", 29);
  return -1;
}

int64_t sys_rm(const char *pathname) {
  if (!current_proc || !pathname)
    return -EFAULT;

  if (minix_fs_is_working())
    return minix_fs_rm(pathname);

  sys_write(STDERR_FILENO, "Error: filesystem not ready\n", 29);
  return -1;
}

int64_t sys_rmdir(const char *pathname) {
  if (!current_proc || !pathname)
    return -EFAULT;

  if (minix_fs_is_working())
    return minix_fs_rmdir(pathname);

  sys_write(STDERR_FILENO, "Error: filesystem not ready\n", 29);
  return -1;
}

// ============================================================================
// PROCESS MANAGEMENT SYSCALLS
// ============================================================================

int64_t sys_fork(void) {
  if (!current_proc)
    return -ESRCH;

  return process_fork();
}

int64_t sys_wait4(pid_t pid, int *status, int options, void *rusage) {
  (void)options;
  (void)rusage;

  if (!current_proc)
    return -ESRCH;

  return process_wait(pid, status);
}

int64_t sys_waitpid(pid_t pid, int *status, int options) {
  return sys_wait4(pid, status, options, NULL);
}

int64_t sys_kernel_info(void *info_buffer, size_t buffer_size) {
  if (!current_proc || !info_buffer)
    return -EFAULT;

  const char *info = "IR0 Kernel v0.0.1 x86-64\n";
  size_t len = 26; // Length of info string

  if (buffer_size < len)
    len = buffer_size;

  // Simple copy without memcpy dependency
  char *dst = (char *)info_buffer;
  for (size_t i = 0; i < len; i++)
    dst[i] = info[i];

  return (int64_t)len;
}

int64_t sys_malloc_test(size_t size) {
  if (!current_proc)
    return -ESRCH;

  // Test malloc/free functionality
  extern void *kmalloc(size_t size);
  extern void kfree(void *ptr);
  extern void simple_alloc_trace(void);

  sys_write(1, "Testing malloc/free...\n", 23);

  // Allocate memory
  void *ptr1 = kmalloc(size ? size : 1024);
  if (!ptr1) {
    sys_write(1, "malloc failed!\n", 15);
    return -1;
  }

  sys_write(1, "malloc OK, ptr=0x", 17);
  print_hex64((uint64_t)ptr1);
  sys_write(1, "\n", 1);

  // Write some data
  char *data = (char *)ptr1;
  for (size_t i = 0; i < (size ? size : 1024) && i < 100; i++) {
    data[i] = 'A' + (i % 26);
  }

  sys_write(1, "Data written, freeing...\n", 25);

  // Free memory
  kfree(ptr1);

  sys_write(1, "free OK\n", 8);

  // Show allocator stats
  simple_alloc_trace();

  return 0;
}

// ============================================================================
// HEAP MANAGEMENT SYSCALLS (brk/sbrk)
// ============================================================================

int64_t sys_brk(void *addr) {
  if (!current_proc)
    return -ESRCH;

  // If addr is NULL, return current break
  if (!addr)
    return (int64_t)current_proc->heap_end;

  // Validate new break address
  if (addr < current_proc->heap_start || addr > current_proc->heap_limit)
    return -EFAULT;

  // Set new break
  current_proc->heap_end = addr;
  return (int64_t)addr;
}

void *sys_sbrk(intptr_t increment) {
  if (!current_proc)
    return (void *)-1;

  void *old_break = current_proc->heap_end;
  void *new_break = (char *)old_break + increment;

  // Check bounds
  if (new_break < current_proc->heap_start ||
      new_break > current_proc->heap_limit)
    return (void *)-1;

  // Update break
  current_proc->heap_end = new_break;
  return old_break;
}

// ============================================================================
// MEMORY MAPPING SYSCALLS (mmap/munmap)
// ============================================================================

// mmap flags
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20
#define MAP_SHARED 0x01

// Protection flags
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4

// Simple memory mapping structure
struct mmap_region {
  void *addr;
  size_t length;
  int prot;
  int flags;
  struct mmap_region *next;
};

static struct mmap_region *mmap_list = NULL;

void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd,
               off_t offset) {
  (void)addr;
  (void)prot;
  (void)fd;
  (void)offset; // Ignore for now

  // Debug output to serial
  serial_print("SERIAL: mmap: entering syscall\n");

  if (!current_proc) {
    serial_print("SERIAL: mmap: no current process\n");
    return (void *)-1;
  }

  if (length == 0) {
    serial_print("SERIAL: mmap: zero length\n");
    return (void *)-1;
  }

  // Debug: show what flags we received
  serial_print("SERIAL: mmap: flags received = ");
  serial_print_hex32((uint32_t)flags);
  serial_print("\n");

  // Only support anonymous mapping for now
  if (!(flags & MAP_ANONYMOUS)) {
    serial_print("SERIAL: mmap: not anonymous mapping\n");
    serial_print("SERIAL: mmap: MAP_ANONYMOUS = 0x20\n");
    return (void *)-1;
  }

  sys_write(1, "mmap: allocating memory\n", 24);

  // Align length to reasonable boundary
  length = (length + 15) & ~15;

  // For simplicity, use kernel allocator to get real memory
  extern void *kmalloc(size_t size);
  void *real_addr = kmalloc(length);
  if (!real_addr) {
    sys_write(1, "mmap: kmalloc failed\n", 21);
    return (void *)-1;
  }

  sys_write(1, "mmap: creating region entry\n", 28);

  // Create mapping entry
  struct mmap_region *region = kmalloc(sizeof(struct mmap_region));
  if (!region) {
    sys_write(1, "mmap: region kmalloc failed\n", 28);
    extern void kfree(void *ptr);
    kfree(real_addr);
    return (void *)-1;
  }

  region->addr = real_addr;
  region->length = length;
  region->prot = prot;
  region->flags = flags;
  region->next = mmap_list;
  mmap_list = region;

  sys_write(1, "mmap: zeroing memory\n", 21);

  // Zero the memory if it's anonymous
  if (flags & MAP_ANONYMOUS) {
    for (size_t i = 0; i < length; i++)
      ((char *)real_addr)[i] = 0;
  }

  sys_write(1, "mmap: success, returning address\n", 33);
  return real_addr;

  return real_addr;
}

int sys_munmap(void *addr, size_t length) {
  if (!current_proc || !addr || length == 0)
    return -1;

  // Find the mapping
  struct mmap_region *current = mmap_list;
  struct mmap_region *prev = NULL;

  while (current) {
    if (current->addr == addr && current->length == length) {
      // Remove from list
      if (prev)
        prev->next = current->next;
      else
        mmap_list = current->next;

      // Free the mapping structure
      extern void kfree(void *ptr);
      kfree(current);
      return 0;
    }
    prev = current;
    current = current->next;
  }

  return -1; // Not found
}

int sys_mprotect(void *addr, size_t len, int prot) {
  if (!current_proc || !addr || len == 0)
    return -1;

  // Find the mapping
  struct mmap_region *current = mmap_list;
  while (current) {
    if (current->addr <= addr &&
        (char *)addr + len <= (char *)current->addr + current->length) {
      // Update protection
      current->prot = prot;
      return 0;
    }
    current = current->next;
  }

  return -1; // Not found
}

// ============================================================================
// SYSCALL INITIALIZATION
// ============================================================================

void syscalls_init(void) {
  // Connect to real process management
  extern void *current_process;
  extern void *get_process_list(void);

  serial_print("SERIAL: syscalls_init: connecting to process management\n");

  // Initialize current process stub with heap
  static struct simple_process init_proc = {
      .pid = 1,
      .state = 1,
      .heap_start = (void *)0x10000000, // 256MB - user heap start
      .heap_end = (void *)0x10000000,   // Initially empty
      .heap_limit = (void *)0x20000000  // 512MB - heap limit (256MB max heap)
  };
  current_proc = &init_proc;

  // Debug: check real process system
  void *real_current = current_process;
  void *real_list = get_process_list();

  serial_print("SERIAL: Real current_process = ");
  serial_print_hex32((uint32_t)(uintptr_t)real_current);
  serial_print("\n");

  serial_print("SERIAL: Real process_list = ");
  serial_print_hex32((uint32_t)(uintptr_t)real_list);
  serial_print("\n");

  // Register syscall interrupt handler
  extern void syscall_entry_asm(void);
  extern void idt_set_gate64(uint8_t num, uint64_t base, uint16_t sel,
                             uint8_t flags);

  // IDT entry 0x80 for syscalls (DPL=3 for user mode)
  idt_set_gate64(0x80, (uint64_t)syscall_entry_asm, 0x08, 0xEE);
}

// Syscall dispatcher called from assembly
int64_t syscall_dispatch(uint64_t syscall_num, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4, uint64_t arg5) {
  // arg4 and arg5 are used by mmap

  switch (syscall_num) {
  case 0:
    return sys_exit((int)arg1);
  case 1:
    return sys_write((int)arg1, (const void *)arg2, (size_t)arg3);
  case 2:
    return sys_read((int)arg1, (void *)arg2, (size_t)arg3);
  case 3:
    return sys_getpid();
  case 4:
    return sys_getppid();
  case 5:
    return sys_ls((const char *)arg1);
  case 6:
    return sys_mkdir((const char *)arg1, (mode_t)arg2);
  case 7:
    return sys_ps();
  case 8:
    return sys_kernel_info((void *)arg1, (size_t)arg2);
  case 9:
    return sys_cat((const char *)arg1);
  case 10:
    return sys_touch((const char *)arg1);
  case 11:
    return sys_rm((const char *)arg1);
  case 12:
    return sys_fork();
  case 13:
    return sys_waitpid((pid_t)arg1, (int *)arg2, (int)arg3);
  case 40:
    return sys_rmdir((const char *)arg1);
  case 50:
    return sys_malloc_test((size_t)arg1);
  case 51:
    return sys_brk((void *)arg1);
  case 52:
    return (int64_t)sys_sbrk((intptr_t)arg1);
  case 53:
    // Debug mmap arguments to serial
    serial_print("SERIAL: dispatcher: mmap args: arg1=");
    serial_print_hex32((uint32_t)arg1);
    serial_print(" arg2=");
    serial_print_hex32((uint32_t)arg2);
    serial_print(" arg3=");
    serial_print_hex32((uint32_t)arg3);
    serial_print(" arg4=");
    serial_print_hex32((uint32_t)arg4);
    serial_print("\n");
    return (int64_t)sys_mmap((void *)arg1, (size_t)arg2, (int)arg3, (int)arg4,
                             (int)arg5, (off_t)0);
  case 54:
    return sys_munmap((void *)arg1, (size_t)arg2);
  case 55:
    return sys_mprotect((void *)arg1, (size_t)arg2, (int)arg3);

  default:
    print("UNKNOWN_SYSCALL:");
    print_hex_compact(syscall_num);
    print("\n");
    return -ENOSYS;
  }
}
