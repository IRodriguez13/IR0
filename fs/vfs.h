// fs/vfs.h - Virtual File System Interface
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
// VFS TYPES AND CONSTANTS
// ===============================================================================

#define VFS_MAX_PATH_LEN 256
#define VFS_MAX_NAME_LEN 64
#define VFS_MAX_OPEN_FILES 128
#define VFS_ROOT_INODE 1
#define VFS_MAX_PATH 256
#define VFS_MAX_NAME 64
#define MAX_OPEN_FILES 128
#define MAX_MOUNTS 16
#define MAX_FILESYSTEMS 8

// VFS inode types
#define VFS_INODE_TYPE_FILE 1
#define VFS_INODE_TYPE_DIRECTORY 2
#define VFS_INODE_TYPE_SYMLINK 3
#define VFS_INODE_TYPE_DEVICE 4

// VFS mount flags
#define VFS_MOUNT_READONLY 0x0001
#define VFS_MOUNT_NOEXEC 0x0002
#define VFS_MOUNT_NOSUID 0x0004

// VFS filesystem types
#define VFS_FS_TYPE_ROOT 1
#define VFS_FS_TYPE_IR0FS 2

// File mode constants
#define S_IFMT  00170000
#define S_IFSOCK 0140000
#define S_IFLNK  0120000
#define S_IFREG  0100000
#define S_IFBLK  0060000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000

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

// Additional types for compatibility
typedef uint32_t mode_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef int64_t off_t;
typedef uint64_t time_t;

// Stat structure
typedef struct {
    uint32_t st_dev;
    uint32_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t st_rdev;
    uint64_t st_size;
    uint32_t st_blksize;
    uint64_t st_blocks;
    time_t st_atime;
    time_t st_mtime;
    time_t st_ctime;
} stat_t;

// Utime buffer structure
typedef struct {
    time_t actime;
    time_t modtime;
} utimbuf_t;

// ===============================================================================
// VFS STRUCTURES
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
    
    // Additional fields for file operations
    uint32_t start_sector; // Starting sector for file data
    uint32_t file_offset;  // Current file offset
    uint64_t modify_time;  // Modification time
    uint64_t access_time;  // Access time
    uint64_t create_time;  // Creation time
    uint32_t inode_number; // Inode number
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
// VFS OPERATIONS INTERFACE
// ===============================================================================

// Filesystem operations
typedef struct vfs_fs_ops
{
    // Inode operations
    int (*read_inode)(vfs_inode_t *inode);
    int (*write_inode)(vfs_inode_t *inode);
    int (*create_inode)(vfs_inode_t *parent, const char *name, vfs_file_type_t type, vfs_inode_t **inode);
    int (*delete_inode)(vfs_inode_t *inode);

    // File operations
    int (*open)(vfs_file_t *file);
    int (*close)(vfs_file_t *file);
    ssize_t (*read)(vfs_file_t *file, void *buf, size_t count);
    ssize_t (*write)(vfs_file_t *file, const void *buf, size_t count);
    int (*seek)(vfs_file_t *file, int64_t offset, vfs_seek_whence_t whence);

    // Directory operations
    int (*readdir)(vfs_file_t *file, vfs_dirent_t *dirent);
    int (*mkdir)(vfs_inode_t *parent, const char *name);
    int (*rmdir)(vfs_inode_t *parent, const char *name);

    // Filesystem operations
    int (*mount)(const char *source, const char *target, const char *fs_type);
    int (*umount)(const char *target);
    int (*sync)(void);
} vfs_fs_ops_t;

// Mount point structure
typedef struct vfs_mount
{
    char path[VFS_MAX_PATH_LEN]; // Mount point path
    char source[VFS_MAX_PATH_LEN]; // Source device/path
    vfs_inode_t *root_inode;     // Root inode of mounted filesystem
    vfs_inode_t *inode;          // Mount point inode
    uint32_t fs_type;            // Filesystem type
    vfs_fs_ops_t *fs_ops;        // Filesystem operations
    uint32_t flags;              // Mount flags
    void *fs_data;               // Filesystem-specific data
    struct vfs_mount *next;      // Next mount point
} vfs_mount_t;

// Filesystem structure
typedef struct vfs_filesystem
{
    char name[32];               // Filesystem name
    uint32_t type;               // Filesystem type
    vfs_fs_ops_t *ops;           // Filesystem operations
} vfs_filesystem_t;

// ===============================================================================
// VFS GLOBAL INTERFACE
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
vfs_inode_t *vfs_get_inode(const char *path);
int vfs_create(const char *path, vfs_file_type_t type);
int vfs_create_inode(const char *path, vfs_file_type_t type);
int vfs_unlink(const char *path);

// Mount operations
int vfs_mount(const char *source, const char *target, const char *fs_type);
int vfs_umount(const char *target);

// Utility functions
char *vfs_basename(const char *path);
char *vfs_dirname(const char *path);
bool vfs_path_is_absolute(const char *path);
int vfs_resolve_path(const char *path, char *resolved_path);
