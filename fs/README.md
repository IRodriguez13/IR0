# IR0 File System

This directory contains the complete file system implementation for IR0, including VFS abstraction and concrete filesystem implementations.

## Components

### Core Files
- `vfs.c/h` - Virtual File System abstraction layer
- `vfs_simple.c/h` - Simplified VFS implementation
- `minix_fs.c/h` - Complete MINIX filesystem implementation

### Build Artifacts
- `libvfs.a` - VFS static library
- `libir0fs.a` - Complete filesystem library

## Features

### Virtual File System (VFS)
- ✅ Unified filesystem abstraction layer
- ✅ Multiple filesystem support framework
- ✅ Standard file operations (open, read, write, close)
- ✅ Directory operations (mkdir, rmdir, opendir, readdir)
- ✅ File metadata handling (stat, chmod, chown)
- ✅ Mount point management
- ✅ Path resolution and traversal

### MINIX File System
- ✅ Complete MINIX filesystem implementation
- ✅ Inode-based file storage
- ✅ Directory structure support
- ✅ File creation, deletion, and modification
- ✅ Superblock management
- ✅ Block allocation and deallocation
- ✅ File size and metadata tracking

### File Operations
- ✅ File creation (`touch` command)
- ✅ File deletion (`rm` command)
- ✅ Directory creation (`mkdir` command)
- ✅ Directory removal (`rmdir` command)
- ✅ File content display (`cat` command)
- ✅ Directory listing (`ls` command)
- ✅ File writing capabilities
- ✅ File reading capabilities

## Architecture

### VFS Layer
```c
// VFS operations structure
struct vfs_operations {
    int (*mount)(const char *source, const char *target, const char *fstype);
    int (*unmount)(const char *target);
    int (*open)(const char *path, int flags, mode_t mode);
    int (*close)(int fd);
    ssize_t (*read)(int fd, void *buf, size_t count);
    ssize_t (*write)(int fd, const void *buf, size_t count);
    int (*mkdir)(const char *path, mode_t mode);
    int (*rmdir)(const char *path);
    int (*unlink)(const char *path);
    int (*stat)(const char *path, struct stat *buf);
};
```

### MINIX Filesystem Structure
```c
// MINIX superblock
struct minix_super_block {
    uint16_t s_ninodes;      // Number of inodes
    uint16_t s_nzones;       // Number of zones
    uint16_t s_imap_blocks;  // Inode map blocks
    uint16_t s_zmap_blocks;  // Zone map blocks
    uint16_t s_firstdatazone; // First data zone
    uint16_t s_log_zone_size; // Log2 of zone size
    uint32_t s_max_size;     // Maximum file size
    uint16_t s_magic;        // Magic number
};

// MINIX inode
struct minix_inode {
    uint16_t i_mode;         // File mode
    uint16_t i_uid;          // User ID
    uint32_t i_size;         // File size
    uint32_t i_time;         // Modification time
    uint8_t i_gid;           // Group ID
    uint8_t i_nlinks;        // Number of links
    uint16_t i_zone[9];      // Zone numbers
};
```

### Integration with Shell
The filesystem is fully integrated with the shell commands:
- `ls` - Lists directory contents using VFS
- `cat` - Displays file contents using VFS read operations
- `mkdir` - Creates directories using VFS mkdir
- `rmdir` - Removes directories using VFS rmdir
- `rm` - Removes files using VFS unlink
- `touch` - Creates empty files using VFS create

## System Call Integration

The filesystem integrates with the kernel's system call interface:
- `SYS_LS(5)` - Directory listing
- `SYS_MKDIR(6)` - Directory creation
- `SYS_WRITE_FILE(8)` - File writing
- `SYS_CAT(9)` - File reading/display
- `SYS_TOUCH(10)` - File creation
- `SYS_RM(11)` - File removal
- `SYS_RMDIR(40)` - Directory removal

## Current Status

### ✅ Fully Implemented
- VFS abstraction layer
- MINIX filesystem support
- Basic file operations (create, read, write, delete)
- Directory operations (create, remove, list)
- Shell integration
- System call interface

### ✅ Working Features
- File creation and deletion
- Directory management
- File content reading and writing
- Path resolution
- Metadata handling

### ⚠️ Limitations
- Single filesystem type (MINIX only)
- No advanced features (symlinks, permissions)
- Basic error handling
- No filesystem checking/repair tools

### 🔄 Potential Improvements
- Additional filesystem support (ext2, FAT32)
- Advanced file permissions
- Symbolic links support
- Filesystem utilities (fsck, mkfs)
- Better error handling and recovery

## Build System

The filesystem is built as static libraries:
- Automatic dependency tracking
- Integration with main kernel build
- Separate library generation for modularity
- Debug symbol support

## Testing

Filesystem functionality can be tested through:
- Shell commands (ls, cat, mkdir, rm, etc.)
- System call interface
- File creation and manipulation
- Directory operations
- Content reading and writing