#ifndef _IR0_TYPES_H
#define _IR0_TYPES_H

#include <stdint.h> // Para tipos estándar

// Standard types
typedef int32_t pid_t;
typedef int64_t time_t; 
typedef int64_t off_t; // Definición centralizada de off_t
typedef uint32_t mode_t;
typedef uint32_t dev_t;
typedef uint32_t ino_t;
typedef uint32_t nlink_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef int64_t blksize_t;
typedef int64_t blkcnt_t;

// File type and permission bits (from stat.h)
#define S_IFMT   0170000  // File type mask
#define S_IFREG  0100000  // Regular file
#define S_IFDIR  0040000  // Directory
#define S_IFCHR  0020000  // Character device
#define S_IFBLK  0060000  // Block device
#define S_IFLNK  0120000  // Symbolic link
#define S_IFSOCK 0140000  // Socket

// File mode bits (from stat.h)
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

#endif /* _IR0_TYPES_H */
