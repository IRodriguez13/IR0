#ifndef KERNEL_FS_FILE_H
#define KERNEL_FS_FILE_H

#include <stdint.h>
#include <stddef.h>
#include <fs/minix_fs.h>

// File descriptor flags
#define FD_CLOEXEC  0x0001  // Close on exec

// File status flags (compatibles con O_*)
#define O_RDONLY    0x0000  // Open for reading only
#define O_WRONLY    0x0001  // Open for writing only
#define O_RDWR      0x0002  // Open for reading and writing
#define O_ACCMODE   0x0003  // Mask for access modes
#define O_CREAT     0x0040  // Create file if it doesn't exist
#define O_EXCL      0x0080  // Fail if file already exists (with O_CREAT)
#define O_TRUNC     0x0200  // Truncate file to zero length if it exists
#define O_APPEND    0x0400  // Append to the end of file on each write

// File types
#define FT_NONE     0  // No file
#define FT_REGULAR  1  // Regular file
#define FT_DIR      2  // Directory
#define FT_DEVICE   3  // Character/block device

// File descriptor structure
typedef struct file_descriptor {
    int fd;                // File descriptor number
    int flags;             // File status flags
    off_t offset;          // Current file offset
    int refcount;          // Reference count
    int type;              // File type (FT_*)
    void *data;            // File-specific data (e.g., inode)
    
    // File operations
    ssize_t (*read)(struct file_descriptor *fd, void *buf, size_t count);
    ssize_t (*write)(struct file_descriptor *fd, const void *buf, size_t count);
    off_t (*lseek)(struct file_descriptor *fd, off_t offset, int whence);
    int (*close)(struct file_descriptor *fd);
} file_descriptor_t;

// File system operations
typedef struct file_operations {
    ssize_t (*read)(file_descriptor_t *fd, void *buf, size_t count);
    ssize_t (*write)(file_descriptor_t *fd, const void *buf, size_t count);
    off_t (*lseek)(file_descriptor_t *fd, off_t offset, int whence);
    int (*open)(file_descriptor_t *fd, const char *path, int flags, mode_t mode);
    int (*close)(file_descriptor_t *fd);
} file_operations_t;

// File system type
typedef struct filesystem_type {
    const char *name;
    file_operations_t *fops;
    struct filesystem_type *next;
} filesystem_type_t;

// File system operations for MINIX
extern file_operations_t minix_file_ops;

// File system functions
int register_filesystem(filesystem_type_t *fs);
file_descriptor_t *get_file_descriptor(int fd);
int alloc_fd(void);
void free_fd(int fd);

#endif // KERNEL_FS_FILE_H
