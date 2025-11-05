#pragma once

#include <stdint.h>
#include <stddef.h>

// File type constants for st_mode
#define S_IFMT   0170000  // File type mask
#define S_IFREG  0100000  // Regular file
#define S_IFDIR  0040000  // Directory
#define S_IFCHR  0020000  // Character device
#define S_IFBLK  0060000  // Block device
#define S_IFLNK  0120000  // Symbolic link
#define S_IFSOCK 0140000  // Socket

// Permission constants
#define S_IRWXU 0000700  // User: read, write, execute
#define S_IRUSR 0000400  // User: read
#define S_IWUSR 0000200  // User: write
#define S_IXUSR 0000100  // User: execute

#define S_IRWXG 0000070  // Group: read, write, execute
#define S_IRGRP 0000040  // Group: read
#define S_IWGRP 0000020  // Group: write
#define S_IXGRP 0000010  // Group: execute

#define S_IRWXO 0000007  // Others: read, write, execute
#define S_IROTH 0000004  // Others: read
#define S_IWOTH 0000002  // Others: write
#define S_IXOTH 0000001  // Others: execute

// Utility macros
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)

typedef struct stat {
    uint16_t st_dev;     // Device ID
    uint16_t st_ino;     // Inode number
    uint16_t st_mode;    // File type and permissions
    uint16_t st_nlink;   // Number of hard links
    uint16_t st_uid;     // User ID
    uint16_t st_gid;     // Group ID
    uint32_t st_size;    // File size in bytes
    uint32_t st_atime;   // Access time
    uint32_t st_mtime;   // Modification time
    uint32_t st_ctime;   // Creation time
} stat_t;