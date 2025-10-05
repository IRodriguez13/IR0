// Syscalls - Essential system calls only
#include "syscalls.h"
#include "../drivers/serial/serial.h"
#include "../includes/ir0/print.h"
#include "process.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Forward declarations for external functions
extern char keyboard_buffer_get(void);
extern int keyboard_buffer_has_data(void);
extern bool minix_fs_is_working(void);
extern int minix_fs_ls(const char *path);
extern int minix_fs_mkdir(const char *path);
extern int minix_fs_cat(const char *path);
extern int minix_fs_write_file(const char *path, const char *content);
extern int minix_fs_touch(const char *path);
extern int minix_fs_rm(const char *path);
extern int minix_fs_rmdir(const char *path);
extern void print_hex_compact(uint32_t num);

// Basic types and constants
typedef uint32_t mode_t;
typedef long intptr_t;
typedef long off_t;

// Process states
#define TASK_ZOMBIE 4

// Errno codes
#define ESRCH 3
#define EBADF 9
#define EFAULT 14
#define ENOSYS 38

// File descriptors
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

// ============================================================================
// SYSCALL IMPLEMENTATIONS - ONLY WORKING ONES
// ============================================================================

int64_t sys_exit(int exit_code)
{
  (void)exit_code;
  if (!current_process)
    return -ESRCH;

  current_process->state = TASK_ZOMBIE;

  // Halt forever
  for (;;)
    __asm__ volatile("hlt");
  return 0;
}

int64_t sys_write(int fd, const void *buf, size_t count)
{
  if (!current_process)
    return -ESRCH;
  if (!buf || count == 0)
    return 0;

  if (fd == STDOUT_FILENO || fd == STDERR_FILENO)
  {
    const char *str = (const char *)buf;
    // Use print directly for simplicity
    for (size_t i = 0; i < count && i < 1024; i++)
    {
      if (str[i] == '\n')
        print("\n");
      else
      {
        char temp[2] = {str[i], 0};
        print(temp);
      }
    }
    return (int64_t)count;
  }
  return -EBADF;
}

// Remove duplicate declarations

int64_t sys_read(int fd, void *buf, size_t count)
{
  if (!current_process)
    return -ESRCH;
  if (!buf || count == 0)
    return 0;

  if (fd == STDIN_FILENO)
  {
    // Read from keyboard buffer - NON-BLOCKING
    char *buffer = (char *)buf;
    size_t bytes_read = 0;

    // Only read if there's data available
    if (keyboard_buffer_has_data())
    {
      char c = keyboard_buffer_get();
      if (c != 0)
      {
        buffer[bytes_read++] = c;
      }
    }

    return (int64_t)bytes_read; // Return 0 if no data available
  }
  return -EBADF;
}

int64_t sys_getpid(void)
{
  if (!current_process)
    return -ESRCH;
  return process_pid(current_process);
}

int64_t sys_getppid(void)
{
  if (!current_process)
    return -ESRCH;
  return 0; // No parent tracking yet
}

int64_t sys_ls(const char *pathname)
{
  if (!current_process)
    return -ESRCH;

  // Use MINIX filesystem directly
  const char *target_path = pathname ? pathname : "/";
  return minix_fs_ls(target_path);
}

int64_t sys_mkdir(const char *pathname, mode_t mode)
{
  (void)mode;
  if (!current_process)
    return -ESRCH;
  if (!pathname)
    return -EFAULT;

  if (minix_fs_is_working())
    return minix_fs_mkdir(pathname);
  return -ENOSYS;
}

// Helper function removed - was unused

int64_t sys_ps(void)
{
  // REAL sys_ps() - uses only real process management

  serial_print("SERIAL: REAL sys_ps() called\n");

  sys_write(1, "PID  STATE    COMMAND\n", 21);
  sys_write(1, "---  -------  -------\n", 22);

  // Get REAL process list
  process_t *proc_list = get_process_list();

  serial_print("SERIAL: Real process_list = ");
  serial_print_hex32((uint32_t)(uintptr_t)proc_list);
  serial_print("\n");

  if (!proc_list)
  {
    serial_print("SERIAL: No processes in list, checking current_process\n");

    // If list is empty but current_process exists, add it
    if (current_process)
    {
      serial_print("SERIAL: Adding current_process to list\n");
      extern process_t *process_list;
      process_list = current_process;
      current_process->next = NULL;
      proc_list = current_process;
    }
  }

  // Show kernel process (PID 0)
  sys_write(1, "  0  IDLE     kernel\n", 20);

  // Iterate through REAL process list
  process_t *proc = proc_list;
  int count = 0;

  while (proc && count < 20)
  {
    serial_print("SERIAL: Processing PID=");
    serial_print_hex32(process_pid(proc));
    serial_print(" state=");
    serial_print_hex32(proc->state);
    serial_print("\n");

    // Show PID
    sys_write(1, "  ", 2);

    // Convert PID to string
    char pid_str[12];
    uint32_t pid = process_pid(proc);
    int len = 0;

    if (pid == 0)
    {
      pid_str[len++] = '0';
    }
    else
    {
      char temp[12];
      int temp_len = 0;
      while (pid > 0)
      {
        temp[temp_len++] = '0' + (pid % 10);
        pid /= 10;
      }
      for (int i = temp_len - 1; i >= 0; i--)
      {
        pid_str[len++] = temp[i];
      }
    }
    pid_str[len] = '\0';

    sys_write(1, pid_str, len);

    // Show state
    switch (proc->state)
    {
    case 0:
      sys_write(1, "  READY   ", 10);
      break;
    case 1:
      sys_write(1, "  RUNNING ", 10);
      break;
    case 2:
      sys_write(1, "  BLOCKED ", 10);
      break;
    case 3:
      sys_write(1, "  SLEEPING", 10);
      break;
    case 4:
      sys_write(1, "  ZOMBIE  ", 10);
      break;
    default:
      sys_write(1, "  UNKNOWN ", 10);
      break;
    }

    // Show command based on PID
    if (process_pid(proc) == 1)
    {
      sys_write(1, " shell\n", 7);
    }
    else if (process_pid(proc) == 0)
    {
      sys_write(1, " kernel\n", 8);
    }
    else
    {
      sys_write(1, " process\n", 9);
    }

    proc = proc->next;
    count++;
  }

  if (count == 0)
  {
    serial_print("SERIAL: No processes found in list\n");
    sys_write(1, "  No processes running\n", 23);
  }
  else
  {
    serial_print("SERIAL: Showed ");
    serial_print_hex32(count);
    serial_print(" processes\n");
  }

  return 0;
}

int64_t sys_cat(const char *pathname)
{
  if (!current_process || !pathname)
    return -EFAULT;

  if (minix_fs_is_working())
    return minix_fs_cat(pathname);

  sys_write(STDERR_FILENO, "Error: filesystem not ready\n", 29);
  return -1;
}

int64_t sys_write_file(const char *pathname, const char *content)
{
  extern void serial_print(const char *str);
  extern void serial_print_hex32(uint32_t num);

  serial_print("SERIAL: sys_write_file called\n");

  if (!current_process)
  {
    serial_print("SERIAL: sys_write_file: no current process\n");
    return -EFAULT;
  }

  if (!pathname)
  {
    serial_print("SERIAL: sys_write_file: pathname is NULL\n");
    return -EFAULT;
  }

  if (!content)
  {
    serial_print("SERIAL: sys_write_file: content is NULL\n");
    return -EFAULT;
  }

  // Validar que los punteros estén en rango válido
  if ((uint64_t)pathname < 0x1000 || (uint64_t)content < 0x1000)
  {
    serial_print("SERIAL: sys_write_file: invalid pointer range\n");
    return -EFAULT;
  }

  serial_print("SERIAL: sys_write_file: pathname=");
  serial_print_hex32((uint32_t)(uintptr_t)pathname);
  serial_print(" content=");
  serial_print_hex32((uint32_t)(uintptr_t)content);
  serial_print("\n");

  if (minix_fs_is_working())
  {
    serial_print("SERIAL: sys_write_file: calling minix_fs_write_file\n");
    return minix_fs_write_file(pathname, content);
  }

  serial_print("SERIAL: sys_write_file: filesystem not ready\n");
  sys_write(STDERR_FILENO, "Error: filesystem not ready\n", 29);
  return -1;
}

int64_t sys_exec(const char *pathname, char *const argv[] __attribute__((unused)), char *const envp[] __attribute__((unused)))
{
  extern void serial_print(const char *str);

  serial_print("SERIAL: sys_exec called\n");

  if (!current_process || !pathname)
  {
    return -EFAULT;
  }

  // For now, simple implementation - load and execute ELF
  extern int elf_load_and_execute(const char *path);
  return elf_load_and_execute(pathname);
}

int64_t sys_touch(const char *pathname)
{
  if (!current_process || !pathname)
    return -EFAULT;

  if (minix_fs_is_working())
    return minix_fs_touch(pathname);

  sys_write(STDERR_FILENO, "Error: filesystem not ready\n", 29);
  return -1;
}

int64_t sys_rm(const char *pathname)
{
  if (!current_process || !pathname)
    return -EFAULT;

  if (minix_fs_is_working())
    return minix_fs_rm(pathname);

  sys_write(STDERR_FILENO, "Error: filesystem not ready\n", 29);
  return -1;
}

int64_t sys_rmdir(const char *pathname)
{
  if (!current_process || !pathname)
    return -EFAULT;

  if (minix_fs_is_working())
    return minix_fs_rmdir(pathname);

  sys_write(STDERR_FILENO, "Error: filesystem not ready\n", 29);
  return -1;
}

// ============================================================================
// PROCESS MANAGEMENT SYSCALLS
// ============================================================================

int64_t sys_fork(void)
{
  if (!current_process)
    return -ESRCH;

  return process_fork();
}

int64_t sys_wait4(pid_t pid, int *status, int options, void *rusage)
{
  (void)options;
  (void)rusage;

  if (!current_process)
    return -ESRCH;

  return process_wait(pid, status);
}

int64_t sys_waitpid(pid_t pid, int *status, int options)
{
  return sys_wait4(pid, status, options, NULL);
}

int64_t sys_kernel_info(void *info_buffer, size_t buffer_size)
{
  if (!current_process || !info_buffer)
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

int64_t sys_malloc_test(size_t size)
{
  if (!current_process)
    return -ESRCH;

  // Test malloc/free functionality
  extern void *kmalloc(size_t size);
  extern void kfree(void *ptr);
  extern void simple_alloc_trace(void);

  sys_write(1, "Testing malloc/free...\n", 23);

  // Allocate memory
  void *ptr1 = kmalloc(size ? size : 1024);
  if (!ptr1)
  {
    sys_write(1, "malloc failed!\n", 15);
    return -1;
  }

  sys_write(1, "malloc OK, ptr=0x", 17);
  print_hex64((uint64_t)ptr1);
  sys_write(1, "\n", 1);

  // Write some data
  char *data = (char *)ptr1;
  for (size_t i = 0; i < (size ? size : 1024) && i < 100; i++)
  {
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

int64_t sys_brk(void *addr)
{
  if (!current_process)
    return -ESRCH;

  // If addr is NULL, return current break
  if (!addr)
    return (int64_t)current_process->heap_end;

  // Validate new break address
  if (addr < (void *)current_process->heap_start ||
      addr > (void *)((char *)current_process->heap_start + 0x10000000))
    return -EFAULT;

  // Set new break
  current_process->heap_end = (uint64_t)addr;
  return (int64_t)addr;
}

void *sys_sbrk(intptr_t increment)
{
  if (!current_process)
    return (void *)-1;

  void *old_break = (void *)current_process->heap_end;
  void *new_break = (char *)old_break + increment;

  // Check bounds (simplified)
  if (new_break < (void *)current_process->heap_start ||
      new_break > (void *)((char *)current_process->heap_start + 0x10000000))
    return (void *)-1;

  // Update break
  current_process->heap_end = (uint64_t)new_break;
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
struct mmap_region
{
  void *addr;
  size_t length;
  int prot;
  int flags;
  struct mmap_region *next;
};

static struct mmap_region *mmap_list = NULL;

void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd,
               off_t offset)
{
  (void)addr;
  (void)prot;
  (void)fd;
  (void)offset; // Ignore for now

  // Debug output to serial
  serial_print("SERIAL: mmap: entering syscall\n");

  if (!current_process)
  {
    serial_print("SERIAL: mmap: no current process\n");
    return (void *)-1;
  }

  if (length == 0)
  {
    serial_print("SERIAL: mmap: zero length\n");
    return (void *)-1;
  }

  // Debug: show what flags we received
  serial_print("SERIAL: mmap: flags received = ");
  serial_print_hex32((uint32_t)flags);
  serial_print("\n");

  // Only support anonymous mapping for now
  if (!(flags & MAP_ANONYMOUS))
  {
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
  if (!real_addr)
  {
    sys_write(1, "mmap: kmalloc failed\n", 21);
    return (void *)-1;
  }

  sys_write(1, "mmap: creating region entry\n", 28);

  // Create mapping entry
  struct mmap_region *region = kmalloc(sizeof(struct mmap_region));
  if (!region)
  {
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
  if (flags & MAP_ANONYMOUS)
  {
    for (size_t i = 0; i < length; i++)
      ((char *)real_addr)[i] = 0;
  }

  sys_write(1, "mmap: success, returning address\n", 33);
  return real_addr;

  return real_addr;
}

int sys_munmap(void *addr, size_t length)
{
  if (!current_process || !addr || length == 0)
    return -1;

  // Find the mapping
  struct mmap_region *current = mmap_list;
  struct mmap_region *prev = NULL;

  while (current)
  {
    if (current->addr == addr && current->length == length)
    {
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

int sys_mprotect(void *addr, size_t len, int prot)
{
  if (!current_process || !addr || len == 0)
    return -1;

  // Find the mapping
  struct mmap_region *current = mmap_list;
  while (current)
  {
    if (current->addr <= addr &&
        (char *)addr + len <= (char *)current->addr + current->length)
    {
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

void syscalls_init(void)
{
  // Connect to REAL process management only
  serial_print("SERIAL: syscalls_init: using REAL process management\n");

  // Debug: check real process system
  process_t *real_current = current_process;
  process_t *real_list = get_process_list();

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
                         uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
  // arg4 and arg5 are used by mmap

  switch (syscall_num)
  {
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
    return sys_write_file((const char *)arg1, (const char *)arg2);
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
  case 56:
    return sys_exec((const char *)arg1, (char *const *)arg2, (char *const *)arg3);
  default:
    print("UNKNOWN_SYSCALL:");
    print_hex_compact(syscall_num);
    print("\n");
    return -ENOSYS;
  }
}
