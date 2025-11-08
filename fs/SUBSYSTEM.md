# Filesystem Subsystem

Virtual File System (VFS) and filesystem implementations.

## Architecture

```
fs/
├── vfs.c/h          - Virtual File System layer
├── vfs_simple.c/h   - Simplified VFS implementation
├── minix_fs.c/h     - MINIX filesystem driver
└── SUBSYSTEM.md     - This file
```

## Components

### VFS Layer (`vfs.h`)

Provides a unified interface for all filesystem operations:
- File operations: open, close, read, write, lseek
- Directory operations: mkdir, rmdir, readdir
- Metadata: stat, fstat
- Path resolution and mounting

### MINIX Filesystem (`minix_fs.h`)

Implementation of MINIX v1/v2 filesystem:
- Inode-based structure
- Direct and indirect blocks
- Directory entries
- Superblock management

## Public Interface

### File Operations
```c
int vfs_open(const char *path, int flags, mode_t mode);
int vfs_close(int fd);
ssize_t vfs_read(int fd, void *buf, size_t count);
ssize_t vfs_write(int fd, const void *buf, size_t count);
off_t vfs_lseek(int fd, off_t offset, int whence);
```

### Directory Operations
```c
int vfs_mkdir(const char *path, mode_t mode);
int vfs_rmdir(const char *path);
int vfs_readdir(const char *path, struct dirent *entries, int max);
```

### Metadata Operations
```c
int vfs_stat(const char *path, struct stat *buf);
int vfs_fstat(int fd, struct stat *buf);
```

### Initialization
```c
void vfs_init(void);
int vfs_init_with_minix(void);
int vfs_mount(const char *device, const char *mountpoint, const char *fstype);
```

## File Descriptor Table

Each process has a file descriptor table (MAX_FDS_PER_PROCESS = 32):
- FD 0: stdin
- FD 1: stdout
- FD 2: stderr
- FD 3-31: User files

## Path Resolution

Paths are resolved relative to the root filesystem:
- Absolute paths start with `/`
- No support for relative paths yet (no current working directory)
- Maximum path length: 256 characters

## Supported Filesystems

### MINIX v1/v2
- Block size: 1024 bytes
- Maximum file size: ~64MB (v1), ~1GB (v2)
- Maximum filename: 14 characters (v1), 30 characters (v2)
- Inode structure with direct/indirect blocks

## Disk Layout (MINIX)

```
+------------------+
| Boot Block       | 1 block
+------------------+
| Superblock       | 1 block
+------------------+
| Inode Bitmap     | Variable
+------------------+
| Zone Bitmap      | Variable
+------------------+
| Inode Table      | Variable
+------------------+
| Data Zones       | Rest of disk
+------------------+
```

## Error Handling

All VFS functions return:
- Non-negative value on success
- Negative errno value on failure (e.g., -ENOENT, -EACCES)

## Caching

Currently no caching implemented. All operations go directly to disk.

## Future Work

- [ ] Buffer cache for disk blocks
- [ ] Inode cache
- [ ] Directory entry cache
- [ ] Write-back caching
- [ ] ext2/ext3/ext4 support
- [ ] FAT32 support
- [ ] Current working directory per process
- [ ] Symbolic links
- [ ] Hard links
- [ ] File permissions enforcement
