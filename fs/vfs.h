// vfs.h - Virtual File System minimalista
#pragma once

#include <ir0/stat.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <ir0/syscall.h> // Para off_t

// Forward declarations
struct vfs_inode;
struct vfs_file;
struct vfs_dentry;

// Operaciones de archivo
struct file_operations {
  int (*open)(struct vfs_inode *inode, struct vfs_file *file);
  int (*read)(struct vfs_file *file, char *buf, size_t count);
  int (*write)(struct vfs_file *file, const char *buf, size_t count);
  int (*close)(struct vfs_file *file);
  off_t (*seek)(struct vfs_file *file, off_t offset, int whence);
  int (*readdir)(struct vfs_file *file, void *dirent,
                 int (*filldir)(void *, const char *, int, long, uint64_t,  unsigned));
};

// Operaciones de inode
struct inode_operations {
  int (*lookup)(struct vfs_inode *dir, const char *name,
                struct vfs_inode **result);
  int (*create)(struct vfs_inode *dir, const char *name, int mode);
  int (*mkdir)(struct vfs_inode *dir, const char *name, int mode);
  int (*unlink)(struct vfs_inode *dir, const char *name);
  int (*getattr)(struct vfs_inode *inode, stat_t *stat);
};

// Operaciones de superblock
struct super_operations {
  int (*read_inode)(struct vfs_inode *inode, uint32_t ino);
  int (*write_inode)(struct vfs_inode *inode);
  int (*delete_inode)(struct vfs_inode *inode);
};

// Tipo de filesystem
struct filesystem_type {
  const char *name;
  int (*mount)(const char *dev_name, const char *dir_name);
  struct filesystem_type *next;
};

// Superblock
struct vfs_superblock {
  struct super_operations *s_op;
  struct filesystem_type *s_type;
  void *s_fs_info; // Datos específicos del FS (ej: minix_fs_info)
};

// Inode
struct vfs_inode {
  uint32_t i_ino;
  uint16_t i_mode;
  uint32_t i_size;
  struct inode_operations *i_op;
  struct file_operations *i_fop;
  struct vfs_superblock *i_sb;
  void *i_private; // Datos específicos del FS
};

// File descriptor
struct vfs_file {
  struct vfs_inode *f_inode;
  uint32_t f_pos;
  int f_flags;
  void *private_data;
};

// VFS API
int vfs_init(void);
int vfs_mount(const char *dev, const char *mountpoint, const char *fstype);
int vfs_open(const char *path, int flags, struct vfs_file **file);
int vfs_read(struct vfs_file *file, char *buf, size_t count);
int vfs_write(struct vfs_file *file, const char *buf, size_t count);
int vfs_append(const char *path, const char *buf, size_t count);
int vfs_close(struct vfs_file *file);
int vfs_ls(const char *path);
int vfs_mkdir(const char *path, int mode);
int vfs_unlink(const char *path);
int vfs_rmdir_recursive(const char *path);
int vfs_stat(const char *path, stat_t *buf);
int vfs_ls_with_stat(const char *path);

// Registro de filesystems
int register_filesystem(struct filesystem_type *fs);
int unregister_filesystem(struct filesystem_type *fs);

// Lookup de paths
struct vfs_inode *vfs_path_lookup(const char *path);

// MINIX filesystem integration
int vfs_init_with_minix(void);

/* Compatibility wrappers (previously in vfs_simple.h):
 * These provide a lightweight API used by some tools/tests. They are
 * thin wrappers around the unified VFS implementation.
 */
void vfs_simple_init(void);
int vfs_simple_mkdir(const char *path);
int vfs_simple_ls(const char *path);
int vfs_simple_create_file(const char *path, const char *filename,
                           uint32_t size);
int vfs_simple_get_directory_count(void);
const char *vfs_simple_get_directory_name(int index);
int vfs_file_exists(const char *pathname);
int vfs_directory_exists(const char *pathname);
int vfs_allocate_sectors(int count);
int vfs_remove_directory(const char *path);