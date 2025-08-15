// fs/vfs_simple.h - Simplified Virtual File System Interface
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Define ssize_t if not available
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef int64_t ssize_t;
#endif

// ===============================================================================
// SIMPLIFIED VFS TYPES
// ===============================================================================

#define VFS_MAX_PATH_LEN 256
#define VFS_MAX_NAME_LEN 64
#define VFS_ROOT_INODE 1

// File types
typedef enum
{
    VFS_TYPE_UNKNOWN = 0,
    VFS_TYPE_REGULAR,   // Regular file
    VFS_TYPE_DIRECTORY, // Directory
    VFS_TYPE_CHARDEV,   // Character device
    VFS_TYPE_BLKDEV,    // Block device
    VFS_TYPE_FIFO,      // Named pipe
    VFS_TYPE_SOCKET,    // Unix domain socket
    VFS_TYPE_SYMLINK    // Symbolic link
} vfs_file_type_t;

// File permissions
typedef enum
{
    VFS_PERM_READ = 0x01,
    VFS_PERM_WRITE = 0x02,
    VFS_PERM_EXEC = 0x04,
    VFS_PERM_OWNER = 0x08,
    VFS_PERM_GROUP = 0x10,
    VFS_PERM_OTHER = 0x20
} vfs_permissions_t;

// File open flags
typedef enum
{
    VFS_O_RDONLY = 0x0000,
    VFS_O_WRONLY = 0x0001,
    VFS_O_RDWR = 0x0002,
    VFS_O_CREAT = 0x0100,
    VFS_O_TRUNC = 0x0200,
    VFS_O_APPEND = 0x0400
} vfs_open_flags_t;

// File seek whence
typedef enum
{
    VFS_SEEK_SET = 0,
    VFS_SEEK_CUR = 1,
    VFS_SEEK_END = 2
} vfs_seek_whence_t;

// ===============================================================================
// SIMPLIFIED VFS STRUCTURES
// ===============================================================================

// Inode structure
typedef struct vfs_inode
{
    uint32_t ino;         // Inode number
    vfs_file_type_t type; // File type
    uint32_t size;        // File size in bytes
    uint32_t blocks;      // Number of blocks
    uint32_t permissions; // File permissions
    uint32_t uid;         // Owner ID
    uint32_t gid;         // Group ID
    uint64_t atime;       // Access time
    uint64_t mtime;       // Modification time
    uint64_t ctime;       // Creation time
    uint32_t links;       // Hard link count
    void *fs_data;        // Filesystem-specific data
    void *private_data;   // Private data for this inode
} vfs_inode_t;

// File descriptor structure
typedef struct vfs_file
{
    vfs_inode_t *inode; // Associated inode
    uint32_t flags;     // Open flags
    uint64_t offset;    // Current file offset
    uint32_t ref_count; // Reference count
    void *private_data; // Private data for this file
} vfs_file_t;

// Directory entry structure
typedef struct vfs_dirent
{
    uint32_t ino;                // Inode number
    uint8_t type;                // File type
    char name[VFS_MAX_NAME_LEN]; // File name
} vfs_dirent_t;

// ===============================================================================
// SIMPLIFIED VFS INTERFACE
// ===============================================================================

// Initialize VFS
int vfs_init(void);

// File operations
int vfs_open(const char *path, uint32_t flags, vfs_file_t **file);
int vfs_close(vfs_file_t *file);
ssize_t vfs_read(vfs_file_t *file, void *buf, size_t count);
ssize_t vfs_write(vfs_file_t *file, const void *buf, size_t count);
int vfs_seek(vfs_file_t *file, int64_t offset, vfs_seek_whence_t whence);

// Directory operations
int vfs_opendir(const char *path, vfs_file_t **file);
int vfs_readdir(vfs_file_t *file, vfs_dirent_t *dirent);
int vfs_mkdir(const char *path);
int vfs_rmdir(const char *path);

// Inode operations
int vfs_lookup(const char *path, vfs_inode_t **inode);
int vfs_create(const char *path, vfs_file_type_t type);
int vfs_unlink(const char *path);

// Utility functions
char *vfs_basename(const char *path);
char *vfs_dirname(const char *path);
bool vfs_path_is_absolute(const char *path);
int vfs_resolve_path(const char *path, char *resolved_path);
