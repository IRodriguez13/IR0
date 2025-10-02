// vfs.h - Virtual File System minimalista
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Forward declarations
struct vfs_inode;
struct vfs_file;
struct vfs_dentry;

// Operaciones de archivo
struct file_operations
{
    int (*open)(struct vfs_inode *inode, struct vfs_file *file);
    int (*read)(struct vfs_file *file, char *buf, size_t count);
    int (*write)(struct vfs_file *file, const char *buf, size_t count);
    int (*close)(struct vfs_file *file);
};

// Operaciones de inode
struct inode_operations
{
    int (*lookup)(struct vfs_inode *dir, const char *name, struct vfs_inode **result);
    int (*create)(struct vfs_inode *dir, const char *name, int mode);
    int (*mkdir)(struct vfs_inode *dir, const char *name, int mode);
    int (*unlink)(struct vfs_inode *dir, const char *name);
};

// Operaciones de superblock
struct super_operations
{
    int (*read_inode)(struct vfs_inode *inode, uint32_t ino);
    int (*write_inode)(struct vfs_inode *inode);
    int (*delete_inode)(struct vfs_inode *inode);
};

// Tipo de filesystem
struct filesystem_type
{
    const char *name;
    int (*mount)(const char *dev_name, const char *dir_name);
    struct filesystem_type *next;
};

// Superblock
struct vfs_superblock
{
    struct super_operations *s_op;
    struct filesystem_type *s_type;
    void *s_fs_info; // Datos específicos del FS (ej: minix_fs_info)
};

// Inode
struct vfs_inode
{
    uint32_t i_ino;
    uint16_t i_mode;
    uint32_t i_size;
    struct inode_operations *i_op;
    struct file_operations *i_fop;
    struct vfs_superblock *i_sb;
    void *i_private; // Datos específicos del FS
};

// File descriptor
struct vfs_file
{
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
int vfs_close(struct vfs_file *file);
int vfs_ls(const char *path);
int vfs_mkdir(const char *path, int mode);
int vfs_unlink(const char *path);

// Registro de filesystems
int register_filesystem(struct filesystem_type *fs);
int unregister_filesystem(struct filesystem_type *fs);

// Lookup de paths
struct vfs_inode *vfs_path_lookup(const char *path);

// MINIX filesystem integration
int vfs_init_with_minix(void);