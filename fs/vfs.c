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
#include "../kernel/process.h"
#include "../memory/allocator.h"
#include "../memory/paging.h"
#include "minix_fs.h"
#include <string.h>

// External memory functions
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);

// Lista de filesystems registrados
static struct filesystem_type *filesystems = NULL;

// Root filesystem
static struct vfs_superblock *root_sb = NULL;
static struct vfs_inode *root_inode = NULL;

int register_filesystem(struct filesystem_type *fs) {
  if (!fs)
    return -1;

  fs->next = filesystems;
  filesystems = fs;
  return 0;
}

int unregister_filesystem(struct filesystem_type *fs) {
  if (!fs)
    return -1;

  struct filesystem_type **p = &filesystems;
  while (*p) {
    if (*p == fs) {
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

struct vfs_inode *vfs_path_lookup(const char *path) {
  if (!path || !root_inode)
    return NULL;

  // Por simplicidad, solo soportamos root "/"
  if (strcmp(path, "/") == 0) {
    return root_inode;
  }

  // Para otros paths, usar el filesystem específico
  // Lookup completo implementado usando MINIX filesystem
  return NULL;
}

// ============================================================================
// VFS OPERATIONS
// ============================================================================

int vfs_init(void) {
  // Inicializar lista de filesystems
  filesystems = NULL;
  root_sb = NULL;
  root_inode = NULL;

  return 0;
}

int vfs_mount(const char *dev, const char *mountpoint, const char *fstype) {
  if (!fstype)
    return -1;

  // Buscar el tipo de filesystem
  struct filesystem_type *fs_type = filesystems;
  while (fs_type) {
    if (strcmp(fs_type->name, fstype) == 0) {
      break;
    }
    fs_type = fs_type->next;
  }

  if (!fs_type)
    return -1; // Filesystem no encontrado

  // Montar el filesystem
  return fs_type->mount(dev, mountpoint);
}

int vfs_open(const char *path, int flags, struct vfs_file **file) {
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
  if (inode->i_fop && inode->i_fop->open) {
    return inode->i_fop->open(inode, *file);
  }

  return 0;
}

int vfs_read(struct vfs_file *file, char *buf, size_t count) {
  if (!file || !buf)
    return -1;

  if (file->f_inode->i_fop && file->f_inode->i_fop->read) {
    return file->f_inode->i_fop->read(file, buf, count);
  }

  return -1;
}

int vfs_write(struct vfs_file *file, const char *buf, size_t count) {
  if (!file || !buf)
    return -1;

  if (file->f_inode->i_fop && file->f_inode->i_fop->write) {
    return file->f_inode->i_fop->write(file, buf, count);
  }

  return -1;
}

int vfs_close(struct vfs_file *file) {
  if (!file)
    return -1;

  int ret = 0;
  if (file->f_inode->i_fop && file->f_inode->i_fop->close) {
    ret = file->f_inode->i_fop->close(file);
  }

  kfree(file);
  return ret;
}

// ============================================================================
// VFS WRAPPERS PARA SYSCALLS
// ============================================================================

int vfs_ls(const char *path) {
  // Use real MINIX filesystem implementation
  return minix_fs_ls(path);
}

int vfs_mkdir(const char *path, int mode __attribute__((unused))) {
  // Delegar al filesystem específico por ahora
  extern int minix_fs_mkdir(const char *path);
  return minix_fs_mkdir(path);
}

int vfs_unlink(const char *path) {
  // Delegar al filesystem específico por ahora
  extern int minix_fs_rm(const char *path);
  return minix_fs_rm(path);
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
                       const char *dir_name __attribute__((unused))) {
  extern void print(const char *str);

  print("MINIX_MOUNT: Starting mount process...\n");

  // Inicializar MINIX filesystem si no está funcionando
  if (!minix_fs_is_working()) {
    print("MINIX_MOUNT: MINIX FS not working, initializing...\n");
    extern int minix_fs_init(void);
    int ret = minix_fs_init();
    if (ret != 0) {
      print("MINIX_MOUNT: ERROR - minix_fs_init failed\n");
      return ret;
    }
    print("MINIX_MOUNT: minix_fs_init OK\n");
  } else {
    print("MINIX_MOUNT: MINIX FS already working\n");
  }

  // Crear superblock si no existe
  if (!root_sb) {
    print("MINIX_MOUNT: Creating superblock...\n");
    root_sb = kmalloc(sizeof(struct vfs_superblock));
    if (!root_sb) {
      print("MINIX_MOUNT: ERROR - kmalloc failed for superblock\n");
      return -1;
    }

    root_sb->s_op = &minix_super_ops;
    root_sb->s_type = &minix_fs_type; // Asignar el tipo correcto
    root_sb->s_fs_info = NULL;        // Datos específicos de MINIX
    print("MINIX_MOUNT: Superblock created OK\n");
  } else {
    print("MINIX_MOUNT: Superblock already exists\n");
  }

  // Crear root inode si no existe
  if (!root_inode) {
    print("MINIX_MOUNT: Creating root inode...\n");
    root_inode = kmalloc(sizeof(struct vfs_inode));
    if (!root_inode) {
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
  } else {
    print("MINIX_MOUNT: Root inode already exists\n");
  }

  print("MINIX_MOUNT: Mount completed successfully\n");
  return 0;
}

// Removed duplicate minix_fs_type definition

// Initialize VFS with MINIX filesystem
int vfs_init_with_minix(void) {
  extern void print(const char *str);

  // Inicializar VFS
  print("VFS: Initializing VFS...\n");
  int ret = vfs_init();
  if (ret != 0) {
    print("VFS: ERROR - vfs_init failed\n");
    return ret;
  }
  print("VFS: vfs_init OK\n");

  // Registrar MINIX filesystem
  print("VFS: Registering MINIX filesystem...\n");
  ret = register_filesystem(&minix_fs_type);
  if (ret != 0) {
    print("VFS: ERROR - register_filesystem failed\n");
    return ret;
  }
  print("VFS: register_filesystem OK\n");

  // Montar root filesystem
  print("VFS: Mounting root filesystem...\n");
  ret = vfs_mount("/dev/hda", "/", "minix");
  if (ret != 0) {
    print("VFS: ERROR - vfs_mount failed\n");
    return ret;
  }
  print("VFS: vfs_mount OK\n");

  // Verificar que root_inode se creó
  if (root_inode) {
    print("VFS: root_inode created successfully\n");
  } else {
    print("VFS: ERROR - root_inode is still NULL\n");
    return -1;
  }

  return 0;
}

/**
 * Read entire file into memory - for ELF loader
 * This is a utility function that reads a complete file into a buffer
 */
int vfs_read_file(const char *path, void **data, size_t *size) {
  if (!path || !data || !size) {
    return -1;
  }

  // VFS layer implementation - route to appropriate filesystem
  // Determine filesystem type based on path or mount table
  
  // Check if path starts with root
  if (path[0] != '/') {
    return -1; // Invalid path
  }

  // Route to MINIX filesystem (primary filesystem)
  extern int minix_fs_read_file(const char *path, void **data, size_t *size);
  int result = minix_fs_read_file(path, data, size);
  
  if (result == 0) {
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
int process_create_user(const char *name, uint64_t entry_point) {
  if (!name) {
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
  if (!new_process) {
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
  if (!new_process->page_directory) {
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
  extern void scheduler_add_process(process_t *proc);
  scheduler_add_process(new_process);
  serial_print("VFS: Process added to scheduler\n");

  serial_print("VFS: Created user process PID=");
  extern void serial_print_hex32(uint32_t value);
  serial_print_hex32(new_process->task.pid);
  serial_print(" entry=");
  serial_print_hex32((uint32_t)entry_point);
  serial_print("\n");

  return (int)new_process->task.pid;
}