// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: vfs.c
 * Description: Virtual File System abstraction layer with MINIX filesystem
 * integration
 */

#include "vfs.h"
#include "minix_fs.h"
#include <kernel/process.h>
#include <ir0/memory/allocator.h>
#include <ir0/memory/paging.h>
#include <string.h>
#include <kernel/rr_sched.h>
#include <stdarg.h>
#include <ir0/fcntl.h> 
#include <ir0/stat.h>  

/* Forward declarations and types */
typedef struct
{
  char name[256];
  uint16_t inode;
  uint8_t type;
} vfs_dirent_t;

static int vfs_readdir(const char *path, vfs_dirent_t *entries, int max_entries);
static int build_path(char *dest, size_t dest_size, const char *dir, const char *name);

// Proper path building without fake snprintf
[[maybe_unused]] static void format_timestamp(uint32_t timestamp, char *buffer, size_t buffer_size)
{
  if (!buffer || buffer_size < 13)
  {
    return;
  }

  if (timestamp == 0)
  {
    strncpy(buffer, "Jan  1 00:00 ", buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    return;
  }

  const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

  uint32_t days_since_epoch = timestamp / 86400;
  uint32_t seconds_today = timestamp % 86400;
  uint32_t hours = seconds_today / 3600;
  uint32_t minutes = (seconds_today % 3600) / 60;

  uint32_t year = 1970;
  uint32_t month = 0;
  uint32_t day = 1;

  uint32_t days_in_year = 365;
  while (days_since_epoch >= days_in_year)
  {
    days_since_epoch -= days_in_year;
    year++;
    days_in_year = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 366 : 365;
  }

  uint32_t days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))
  {
    days_in_month[1] = 29;
  }

  while (days_since_epoch >= days_in_month[month])
  {
    days_since_epoch -= days_in_month[month];
    month++;
    if (month >= 12)
    {
      month = 0;
      year++;
    }
  }
  day = days_since_epoch + 1;

  buffer[0] = months[month][0];
  buffer[1] = months[month][1];
  buffer[2] = months[month][2];
  buffer[3] = ' ';
  buffer[4] = ' ';
  if (day >= 10)
  {
    buffer[4] = '0' + (day / 10);
  }
  buffer[5] = '0' + (day % 10);
  buffer[6] = ' ';
  buffer[7] = '0' + (hours / 10);
  buffer[8] = '0' + (hours % 10);
  buffer[9] = ':';
  buffer[10] = '0' + (minutes / 10);
  buffer[11] = '0' + (minutes % 10);
  buffer[12] = ' ';
  buffer[13] = '\0';
}

static int build_path(char *dest, size_t dest_size, const char *dir, const char *name)
{
  if (!dest || !dir || !name || dest_size == 0)
  {
    return -1;
  }

  size_t dir_len = strlen(dir);
  size_t name_len = strlen(name);
  size_t total_len = dir_len + name_len + 2; // +2 for '/' and '\0'

  if (total_len > dest_size)
  {
    return -1; // Path too long
  }

  size_t i = 0;

  // Copy directory
  for (size_t j = 0; j < dir_len && i < dest_size - 1; j++)
  {
    dest[i++] = dir[j];
  }

  // Add separator if needed
  if (dir_len > 0 && dir[dir_len - 1] != '/' && i < dest_size - 1)
  {
    dest[i++] = '/';
  }

  // Copy filename
  for (size_t j = 0; j < name_len && i < dest_size - 1; j++)
  {
    dest[i++] = name[j];
  }

  dest[i] = '\0';
  return 0;
}

// External memory functions
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);

// Lista de filesystems registrados
static struct filesystem_type *filesystems = NULL;

// Root filesystem
static struct vfs_superblock *root_sb = NULL;
static struct vfs_inode *root_inode = NULL;

int register_filesystem(struct filesystem_type *fs)
{
  if (!fs)
    return -1;

  fs->next = filesystems;
  filesystems = fs;
  return 0;
}

int unregister_filesystem(struct filesystem_type *fs)
{
  if (!fs)
    return -1;

  struct filesystem_type **p = &filesystems;
  while (*p)
  {
    if (*p == fs)
    {
      *p = fs->next;
      return 0;
    }
    p = &(*p)->next;
  }
  return -1;
}

// ============================================================================
// PATH LOOKUP
// ============================================================================

struct vfs_inode *vfs_path_lookup(const char *path)
{
  if (!path || !root_inode)
    return NULL;

  // Por simplicidad, solo soportamos root "/"
  if (strcmp(path, "/") == 0)
  {
    return root_inode;
  }

  // Para otros paths, usar el filesystem específico
  // Lookup completo implementado usando MINIX filesystem
  return NULL;
}

// ============================================================================
// VFS OPERATIONS
// ============================================================================

int vfs_init(void)
{
  // Inicializar lista de filesystems
  filesystems = NULL;
  root_sb = NULL;
  root_inode = NULL;

  return 0;
}

int vfs_mount(const char *dev, const char *mountpoint, const char *fstype)
{
  if (!fstype)
    return -1;

  // Buscar el tipo de filesystem
  struct filesystem_type *fs_type = filesystems;
  while (fs_type)
  {
    if (strcmp(fs_type->name, fstype) == 0)
    {
      break;
    }
    fs_type = fs_type->next;
  }

  if (!fs_type)
    return -1; // Filesystem no encontrado

  // Montar el filesystem
  return fs_type->mount(dev, mountpoint);
}

int vfs_open(const char *path, int flags, struct vfs_file **file)
{
  if (!path || !file)
    return -1;

  struct vfs_inode *inode = vfs_path_lookup(path);
  if (!inode)
    return -1;

  // Crear file descriptor
  *file = kmalloc(sizeof(struct vfs_file));
  if (!*file)
    return -1;

  (*file)->f_inode = inode;
  (*file)->f_pos = 0;
  (*file)->f_flags = flags;
  (*file)->private_data = NULL;

  // Llamar a open del filesystem específico
  if (inode->i_fop && inode->i_fop->open)
  {
    return inode->i_fop->open(inode, *file);
  }

  return 0;
}

int vfs_read(struct vfs_file *file, char *buf, size_t count)
{
  if (!file || !buf)
    return -1;

  if (file->f_inode->i_fop && file->f_inode->i_fop->read)
  {
    return file->f_inode->i_fop->read(file, buf, count);
  }

  return -1;
}

int vfs_write(struct vfs_file *file, const char *buf, size_t count)
{
  if (!file || !buf)
    return -1;

  if (file->f_inode->i_fop && file->f_inode->i_fop->write)
  {
    return file->f_inode->i_fop->write(file, buf, count);
  }

  return -1;
}

int vfs_append(const char *path, const char *buf, size_t count)
{
  if (!path || !buf)
    return -1;

  struct vfs_file *file;
  int ret = vfs_open(path, O_WRONLY | O_APPEND, &file);
  if (ret != 0)
    return ret;

  // Move to end of file
  if (file->f_inode->i_fop && file->f_inode->i_fop->seek)
  {
    file->f_inode->i_fop->seek(file, 0, SEEK_END);
  }
  else
  {
    file->f_pos = file->f_inode->i_size;
  }

  // Write data
  ret = vfs_write(file, buf, count);

  vfs_close(file);
  return ret;
}

int vfs_close(struct vfs_file *file)
{
  if (!file)
    return -1;

  int ret = 0;
  if (file->f_inode->i_fop && file->f_inode->i_fop->close)
  {
    ret = file->f_inode->i_fop->close(file);
  }

  kfree(file);
  return ret;
}

// ============================================================================
// VFS WRAPPERS PARA SYSCALLS
// ============================================================================

int vfs_ls(const char *path)
{
  // Use real MINIX filesystem implementation
  return minix_fs_ls(path, false);
}

int vfs_mkdir(const char *path, int mode)
{
  // Delegar al filesystem específico por ahora
  extern int minix_fs_mkdir(const char *path, mode_t mode);
  return minix_fs_mkdir(path, (mode_t)mode);
}

int vfs_unlink(const char *path)
{
  // Delegar al filesystem específico por ahora
  extern int minix_fs_rm(const char *path);
  return minix_fs_rm(path);
}

int vfs_rmdir_recursive(const char *path)
{
  extern int64_t sys_write(int fd, const void *buf, size_t count);

  if (!path)
  {
    return -1;
  }

  // Check if path is valid and not root
  if (path[0] == '\0' || (path[0] == '/' && path[1] == '\0'))
  {
    sys_write(2, "rm: cannot remove root directory\n", 33);
    return -1;
  }

  // Read directory contents
  vfs_dirent_t entries[64];
  int entry_count = vfs_readdir(path, entries, 64);

  if (entry_count < 0)
  {
    // Not a directory, try to remove as file
    return vfs_unlink(path);
  }

  // Recursively delete all entries
  for (int i = 0; i < entry_count; i++)
  {
    // Skip . and .. - check both name and first character
    if (entries[i].name[0] == '\0')
    {
      continue;
    }

    if (entries[i].name[0] == '.' &&
        (entries[i].name[1] == '\0' ||
         (entries[i].name[1] == '.' && entries[i].name[2] == '\0')))
    {
      continue;
    }

    // Build full path
    char full_path[512];
    if (build_path(full_path, sizeof(full_path), path, entries[i].name) != 0)
    {
      continue;
    }

    // Prevent infinite recursion - check if we're trying to delete parent
    if (strcmp(full_path, path) == 0)
    {
      continue;
    }

    // Check if it's a directory
    stat_t st;
    if (vfs_stat(full_path, &st) == 0)
    {
      if (S_ISDIR(st.st_mode))
      {
        // Recursively delete subdirectory
        if (vfs_rmdir_recursive(full_path) != 0)
        {
          sys_write(2, "rm: failed to remove subdirectory: ", 35);
          sys_write(2, full_path, strlen(full_path));
          sys_write(2, "\n", 1);
          return -1;
        }
      }
      else
      {
        // Delete file
        if (vfs_unlink(full_path) != 0)
        {
          sys_write(2, "rm: failed to remove file: ", 27);
          sys_write(2, full_path, strlen(full_path));
          sys_write(2, "\n", 1);
          return -1;
        }
      }
    }
  }

  // Finally, remove the now-empty directory
  extern int minix_fs_rmdir(const char *path);
  return minix_fs_rmdir(path);
}

int vfs_stat(const char *path, stat_t *buf)
{
  if (!path || !buf)
  {
    return -1;
  }

  /*
   * Route stat request through VFS layer to appropriate filesystem.
   * Currently all requests are handled by the MINIX filesystem.
   */
  extern int minix_fs_stat(const char *pathname, stat_t *buf);
  return minix_fs_stat(path, buf);
}

static int vfs_readdir(const char *path, vfs_dirent_t *entries, int max_entries)
{
  if (!path || !entries || max_entries <= 0)
  {
    return -1;
  }

  extern bool minix_fs_is_working(void);
  extern minix_inode_t *minix_fs_find_inode(const char *pathname);
  extern bool minix_is_dir(const minix_inode_t *inode);
  extern int minix_read_block(uint32_t block_num, void *buffer);

  if (!minix_fs_is_working())
  {
    return -1;
  }

  minix_inode_t *dir_inode = minix_fs_find_inode(path);
  if (!dir_inode || !minix_is_dir(dir_inode))
  {
    return -1;
  }

  int entry_count = 0;

  for (int i = 0; i < 7 && entry_count < max_entries; i++)
  {
    if (dir_inode->i_zone[i] == 0)
    {
      continue;
    }

    uint8_t block_buffer[MINIX_BLOCK_SIZE];
    if (minix_read_block(dir_inode->i_zone[i], block_buffer) != 0)
    {
      continue;
    }

    typedef struct
    {
      uint16_t inode;
      char name[14];
    } minix_dir_entry_t;

    minix_dir_entry_t *minix_entries = (minix_dir_entry_t *)block_buffer;
    int num_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);

    for (int j = 0; j < num_entries && entry_count < max_entries; j++)
    {
      if (minix_entries[j].inode == 0)
      {
        continue;
      }

      strncpy(entries[entry_count].name, minix_entries[j].name, sizeof(entries[entry_count].name) - 1);
      entries[entry_count].name[sizeof(entries[entry_count].name) - 1] = '\0';
      entries[entry_count].inode = minix_entries[j].inode;
      entries[entry_count].type = 0;
      entry_count++;
    }
  }

  return entry_count;
}

int vfs_ls_with_stat(const char *path)
{
  // Use the detailed flag in minix_fs_ls
  return minix_fs_ls(path, true);
}

// Forward declarations for MINIX filesystem functions
extern bool minix_fs_is_working(void);
extern int minix_fs_init(void);
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);

// Forward declaration for mount function
static int minix_mount(const char *dev_name, const char *dir_name);

// Operaciones de archivo para MINIX - Implementadas via syscalls
static struct file_operations minix_file_ops = {
    .open = NULL,  // Implementado via sys_open
    .read = NULL,  // Implementado via sys_read
    .write = NULL, // Implementado via sys_write
    .close = NULL, // Implementado via sys_close
};

// Operaciones de inode para MINIX - Implementadas via syscalls
static struct inode_operations minix_inode_ops = {
    .lookup = NULL, // Implementado via minix_fs_find_inode
    .create = NULL, // Implementado via sys_creat/sys_touch
    .mkdir = NULL,  // Implementado via sys_mkdir
    .unlink = NULL, // Implementado via sys_unlink
};

// Operaciones de superblock para MINIX - Implementadas via MINIX FS
static struct super_operations minix_super_ops = {
    .read_inode = NULL,   // Implementado via minix_fs_find_inode
    .write_inode = NULL,  // Implementado via minix_fs_write_file
    .delete_inode = NULL, // Implementado via minix_fs_rm
};

// MINIX filesystem type definition
static struct filesystem_type minix_fs_type = {
    .name = "minix", .mount = minix_mount, .next = NULL};

// Mount function para MINIX
static int minix_mount(const char *dev_name __attribute__((unused)),
                       const char *dir_name __attribute__((unused)))
{
  extern void print(const char *str);

  print("MINIX_MOUNT: Starting mount process...\n");

  // Inicializar MINIX filesystem si no está funcionando
  if (!minix_fs_is_working())
  {
    print("MINIX_MOUNT: MINIX FS not working, initializing...\n");
    extern int minix_fs_init(void);
    int ret = minix_fs_init();
    if (ret != 0)
    {
      print("MINIX_MOUNT: ERROR - minix_fs_init failed\n");
      return ret;
    }
    print("MINIX_MOUNT: minix_fs_init OK\n");
  }
  else
  {
    print("MINIX_MOUNT: MINIX FS already working\n");
  }

  // Crear superblock si no existe
  if (!root_sb)
  {
    print("MINIX_MOUNT: Creating superblock...\n");
    root_sb = kmalloc(sizeof(struct vfs_superblock));
    if (!root_sb)
    {
      print("MINIX_MOUNT: ERROR - kmalloc failed for superblock\n");
      return -1;
    }

    root_sb->s_op = &minix_super_ops;
    root_sb->s_type = &minix_fs_type; // Asignar el tipo correcto
    root_sb->s_fs_info = NULL;        // Datos específicos de MINIX
    print("MINIX_MOUNT: Superblock created OK\n");
  }
  else
  {
    print("MINIX_MOUNT: Superblock already exists\n");
  }

  // Crear root inode si no existe
  if (!root_inode)
  {
    print("MINIX_MOUNT: Creating root inode...\n");
    root_inode = kmalloc(sizeof(struct vfs_inode));
    if (!root_inode)
    {
      print("MINIX_MOUNT: ERROR - kmalloc failed for root_inode\n");
      kfree(root_sb);
      root_sb = NULL;
      return -1;
    }

    root_inode->i_ino = 1;               // Root inode number
    root_inode->i_mode = 0040755;        // Directory with 755 permissions
    root_inode->i_size = 0;              // Directory size
    root_inode->i_op = &minix_inode_ops; // Inode operations
    root_inode->i_fop = &minix_file_ops; // File operations
    root_inode->i_sb = root_sb;          // Superblock reference
    root_inode->i_private = NULL;        // No private data
    print("MINIX_MOUNT: Root inode created OK\n");
  }
  else
  {
    print("MINIX_MOUNT: Root inode already exists\n");
  }

  print("MINIX_MOUNT: Mount completed successfully\n");
  return 0;
}

// Removed duplicate minix_fs_type definition

// Initialize VFS with MINIX filesystem
int vfs_init_with_minix(void)
{
  extern void print(const char *str);

  // Inicializar VFS
  print("VFS: Initializing VFS...\n");
  int ret = vfs_init();
  if (ret != 0)
  {
    print("VFS: ERROR - vfs_init failed\n");
    return ret;
  }
  print("VFS: vfs_init OK\n");

  // Registrar MINIX filesystem
  print("VFS: Registering MINIX filesystem...\n");
  ret = register_filesystem(&minix_fs_type);
  if (ret != 0)
  {
    print("VFS: ERROR - register_filesystem failed\n");
    return ret;
  }
  print("VFS: register_filesystem OK\n");

  // Montar root filesystem
  print("VFS: Mounting root filesystem...\n");
  ret = vfs_mount("/dev/hda", "/", "minix");
  if (ret != 0)
  {
    print("VFS: ERROR - vfs_mount failed\n");
    return ret;
  }
  print("VFS: vfs_mount OK\n");

  // Verificar que root_inode se creó
  if (root_inode)
  {
    print("VFS: root_inode created successfully\n");
  }
  else
  {
    print("VFS: ERROR - root_inode is still NULL\n");
    return -1;
  }

  return 0;
}

/**
 * Read entire file into memory - for ELF loader
 * This is a utility function that reads a complete file into a buffer
 */
int vfs_read_file(const char *path, void **data, size_t *size)
{
  if (!path || !data || !size)
  {
    return -1;
  }

  // VFS layer implementation - route to appropriate filesystem
  // Determine filesystem type based on path or mount table

  // Check if path starts with root
  if (path[0] != '/')
  {
    return -1; // Invalid path
  }

  // Route to MINIX filesystem (primary filesystem)
  extern int minix_fs_read_file(const char *path, void **data, size_t *size);
  int result = minix_fs_read_file(path, data, size);

  if (result == 0)
  {
    // File read successfully through VFS
    extern void serial_print(const char *str);
    serial_print("VFS: File read successfully: ");
    serial_print(path);
    serial_print("\n");
  }

  return result;
}

/**
 * Create user process - for ELF loader
 * This creates a new process structure for user programs
 */
int process_create_user(const char *name, uint64_t entry_point)
{
  if (!name)
  {
    return -1;
  }

  extern void serial_print(const char *str);
  serial_print("VFS: Creating real user process for ");
  serial_print(name);
  serial_print("\n");

  // Allocate memory for new process
  extern void *kmalloc(size_t size);
  extern void kfree(void *ptr);

  process_t *new_process = (process_t *)kmalloc(sizeof(process_t));
  if (!new_process)
  {
    serial_print("VFS: Failed to allocate process structure\n");
    return -1;
  }

  // Initialize process structure
  extern void *memset(void *s, int c, size_t n);
  memset(new_process, 0, sizeof(process_t));

  // Set up basic process info
  static pid_t next_user_pid = 100;
  new_process->task.pid = next_user_pid++;
  new_process->ppid = 1; // Init process as parent
  new_process->state = PROCESS_READY;
  new_process->task.state = TASK_READY;
  new_process->task.priority = 128; // Default priority
  new_process->task.nice = 0;       // Default nice value

  // Set up user mode segments
  new_process->task.cs = 0x1B; // User code segment (GDT entry 3, RPL=3)
  new_process->task.ss = 0x23; // User data segment (GDT entry 4, RPL=3)
  new_process->task.ds = 0x23;
  new_process->task.es = 0x23;
  new_process->task.fs = 0x23;
  new_process->task.gs = 0x23;

  // Set up entry point
  new_process->task.rip = entry_point;
  new_process->task.rflags = 0x202; // Interrupts enabled, IOPL=0

// Allocate user stack (4MB at high address)
#define USER_STACK_SIZE (4 * 1024 * 1024) // 4MB
#define USER_STACK_BASE 0x7FFFF000        // High user address

  new_process->stack_start = USER_STACK_BASE;
  new_process->stack_size = USER_STACK_SIZE;
  new_process->task.rsp = USER_STACK_BASE; // Stack grows down
  new_process->task.rbp = USER_STACK_BASE;

  // Set up heap (starts at 32MB)
  new_process->heap_start = 0x2000000; // 32MB
  new_process->heap_end = 0x2000000;   // Initially empty

  // Create page directory for user process
  new_process->page_directory = (uint64_t *)create_process_page_directory();
  if (!new_process->page_directory)
  {
    serial_print("VFS: Failed to create page directory\n");
    kfree(new_process);
    return -1;
  }

  new_process->task.cr3 = (uint64_t)new_process->page_directory;

  // Add to global process list
  extern process_t *process_list;
  new_process->next = process_list;
  process_list = new_process;

  // Add to scheduler
  rr_add_process(new_process);
  serial_print("VFS: Process added to scheduler\n");

  serial_print("VFS: Created user process PID=");
  extern void serial_print_hex32(uint32_t value);
  serial_print_hex32(new_process->task.pid);
  serial_print(" entry=");
  serial_print_hex32((uint32_t)entry_point);
  serial_print("\n");

  return (int)new_process->task.pid;
}