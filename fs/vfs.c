// fs/vfs.c - Virtual File System Implementation (Production Level)
#include "vfs.h"
#include "ir0fs.h"
#include "../includes/ir0/print.h"
#include "../includes/ir0/panic/panic.h"
#include "../includes/string.h"
#include "../memory/memo_interface.h"
#include "../memory/heap_allocator.h"
#include <string.h>

// ===============================================================================
// GLOBAL VFS STATE
// ===============================================================================

// External filesystem operations
extern vfs_fs_ops_t ir0fs_ops;

static vfs_mount_t *vfs_mount_list = NULL;
static vfs_file_t *vfs_open_files[VFS_MAX_OPEN_FILES];
static vfs_inode_t *vfs_root_inode = NULL;
static bool vfs_initialized = false;

// Global variables for the current implementation
static vfs_inode_t vfs_root;
static vfs_mount_t vfs_mounts[MAX_MOUNTS];
static vfs_file_t vfs_files[MAX_OPEN_FILES];
static vfs_filesystem_t vfs_filesystems[MAX_FILESYSTEMS];
static int vfs_mount_count = 0;
static int vfs_filesystem_count = 0;

// Filesystem registry
typedef struct vfs_fs_registry {
    char name[32];
    vfs_fs_ops_t *ops;
    struct vfs_fs_registry *next;
} vfs_fs_registry_t;

static vfs_fs_registry_t *vfs_registered_fs = NULL;

// Cache management
typedef struct vfs_cache_entry {
    uint32_t ino;
    vfs_inode_t *inode;
    uint64_t last_access;
    uint32_t ref_count;
    struct vfs_cache_entry *next;
    struct vfs_cache_entry *prev;
} vfs_cache_entry_t;

static vfs_cache_entry_t *vfs_inode_cache = NULL;
static uint32_t vfs_cache_size = 0;
static const uint32_t VFS_MAX_CACHE_SIZE = 1000;

// ===============================================================================
// INTERNAL UTILITY FUNCTIONS
// ===============================================================================

// Find free file descriptor
static int vfs_find_free_fd(void)
{
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++)
    {
        if (vfs_open_files[i] == NULL)
        {
            return i;
        }
    }
    return -1;
}

// Get file descriptor by index
static vfs_file_t *vfs_get_file(int fd)
{
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES)
    {
        return NULL;
    }
    return vfs_open_files[fd];
}

// Set file descriptor
static int vfs_set_file(int fd, vfs_file_t *file)
{
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES)
    {
        return -1;
    }
    vfs_open_files[fd] = file;
    return fd;
}

// Split path into parent and name
static int vfs_split_path(const char *path, char *parent, char *name)
{
    if (!path || !parent || !name) {
        return -1;
    }
    
    const char *last_slash = strrchr(path, '/');
    if (!last_slash) {
        return -1;
    }
    
    // Copy parent path
    size_t parent_len = last_slash - path;
    if (parent_len == 0) {
        strcpy(parent, "/");
    } else {
        strncpy(parent, path, parent_len);
        parent[parent_len] = '\0';
    }
    
    // Copy name
    strcpy(name, last_slash + 1);
    
    return 0;
}

// Find mount point for a path
static vfs_mount_t *vfs_find_mount(const char *path)
{
    for (int i = 0; i < vfs_mount_count; i++) {
        if (strncmp(path, vfs_mounts[i].path, strlen(vfs_mounts[i].path)) == 0) {
            return &vfs_mounts[i];
        }
    }
    return NULL;
}

// Find mount point by inode
static vfs_mount_t *vfs_find_mount_by_inode(vfs_inode_t *inode)
{
    for (int i = 0; i < vfs_mount_count; i++) {
        if (vfs_mounts[i].inode == inode) {
            return &vfs_mounts[i];
        }
    }
    return NULL;
}

// Find filesystem by name
static vfs_filesystem_t *vfs_find_filesystem(const char *name)
{
    for (int i = 0; i < vfs_filesystem_count; i++) {
        if (strcmp(vfs_filesystems[i].name, name) == 0) {
            return &vfs_filesystems[i];
        }
    }
    return NULL;
}

// ===============================================================================
// FILESYSTEM REGISTRATION
// ===============================================================================

int vfs_register_filesystem(const char *name, vfs_fs_ops_t *ops)
{
    if (!name || !ops) {
        return -1;
    }
    
    vfs_fs_registry_t *new_fs = kmalloc(sizeof(vfs_fs_registry_t));
    if (!new_fs) {
        return -1;
    }
    
    strncpy(new_fs->name, name, sizeof(new_fs->name) - 1);
    new_fs->name[sizeof(new_fs->name) - 1] = '\0';
    new_fs->ops = ops;
    new_fs->next = vfs_registered_fs;
    vfs_registered_fs = new_fs;
    
    print_success("Filesystem registered\n");
    return 0;
}

vfs_fs_ops_t *vfs_get_filesystem_ops(const char *name)
{
    vfs_fs_registry_t *fs = vfs_registered_fs;
    while (fs) {
        if (strcmp(fs->name, name) == 0) {
            return fs->ops;
        }
        fs = fs->next;
    }
    return NULL;
}

// ===============================================================================
// INODE CACHE MANAGEMENT
// ===============================================================================

vfs_inode_t *vfs_cache_get_inode(uint32_t ino)
{
    vfs_cache_entry_t *entry = vfs_inode_cache;
    while (entry) {
        if (entry->ino == ino) {
            entry->last_access = 0; // TODO: Get current time
            entry->ref_count++;
            return entry->inode;
        }
        entry = entry->next;
    }
    return NULL;
}

void vfs_cache_put_inode(uint32_t ino, vfs_inode_t *inode)
{
    if (!inode) {
        return;
    }
    
    // Check if already in cache
    if (vfs_cache_get_inode(ino)) {
        return;
    }
    
    // Create new cache entry
    vfs_cache_entry_t *entry = kmalloc(sizeof(vfs_cache_entry_t));
    if (!entry) {
        return;
    }
    
    entry->ino = ino;
    entry->inode = inode;
    entry->last_access = 0; // TODO: Get current time
    entry->ref_count = 1;
    entry->next = vfs_inode_cache;
    entry->prev = NULL;
    
    if (vfs_inode_cache) {
        vfs_inode_cache->prev = entry;
    }
    vfs_inode_cache = entry;
    vfs_cache_size++;
    
    // Evict old entries if cache is full
    if (vfs_cache_size > VFS_MAX_CACHE_SIZE) {
        vfs_cache_entry_t *oldest = vfs_inode_cache;
        vfs_cache_entry_t *current = vfs_inode_cache;
        
        while (current) {
            if (current->last_access < oldest->last_access) {
                oldest = current;
            }
            current = current->next;
        }
        
        if (oldest) {
            // Remove from cache
            if (oldest->prev) {
                oldest->prev->next = oldest->next;
            } else {
                vfs_inode_cache = oldest->next;
            }
            if (oldest->next) {
                oldest->next->prev = oldest->prev;
            }
            
            kfree(oldest->inode);
            kfree(oldest);
            vfs_cache_size--;
        }
    }
}

void vfs_cache_remove_inode(uint32_t ino)
{
    vfs_cache_entry_t *entry = vfs_inode_cache;
    while (entry) {
        if (entry->ino == ino) {
            if (entry->prev) {
                entry->prev->next = entry->next;
            } else {
                vfs_inode_cache = entry->next;
            }
            if (entry->next) {
                entry->next->prev = entry->prev;
            }
            
            kfree(entry->inode);
            kfree(entry);
            vfs_cache_size--;
            return;
        }
        entry = entry->next;
    }
}

// ===============================================================================
// PATH MANIPULATION UTILITIES
// ===============================================================================

// Path manipulation utilities
char *vfs_basename(const char *path)
{
    if (!path)
        return NULL;

    const char *last_slash = strrchr(path, '/');
    if (!last_slash)
    {
        return (char *)path;
    }

    return (char *)(last_slash + 1);
}

char *vfs_dirname(const char *path)
{
    if (!path)
        return NULL;

    const char *last_slash = strrchr(path, '/');
    if (!last_slash)
    {
        return (char *)".";
    }

    // Create a copy of the path up to the last slash
    size_t len = last_slash - path;
    char *dir = kmalloc(len + 1);
    if (!dir)
        return NULL;

    strncpy(dir, path, len);
    dir[len] = '\0';

    return dir;
}

bool vfs_path_is_absolute(const char *path)
{
    return path && path[0] == '/';
}

int vfs_resolve_path(const char *path, char *resolved_path)
{
    if (!path || !resolved_path)
    {
        return -1;
    }

    // For now, just copy the path (simplified)
    strcpy(resolved_path, path);
    return 0;
}

// ===============================================================================
// VFS IMPLEMENTATION WITH REAL FUNCTIONALITY
// ===============================================================================

int vfs_init(void)
{
    // Initialize VFS structures
    memset(&vfs_root, 0, sizeof(vfs_inode_t));
    memset(&vfs_mounts, 0, sizeof(vfs_mount_t) * MAX_MOUNTS);
    memset(&vfs_files, 0, sizeof(vfs_file_t) * MAX_OPEN_FILES);
    
    // Initialize root inode
    vfs_root.ino = 1;
    vfs_root.type = VFS_INODE_TYPE_DIRECTORY;
    vfs_root.permissions = 0755;
    vfs_root.uid = 0;
    vfs_root.gid = 0;
    vfs_root.size = 0;
    vfs_root.links = 1;
    vfs_root.atime = 0;
    vfs_root.mtime = 0;
    vfs_root.ctime = 0;
    // vfs_inode_t doesn't have a name field
    
    // Initialize mount point for root
    vfs_mounts[0].inode = &vfs_root;
    vfs_mounts[0].fs_type = VFS_FS_TYPE_ROOT;
    vfs_mounts[0].flags = VFS_MOUNT_READONLY;
    strcpy(vfs_mounts[0].path, "/");
    vfs_mount_count = 1;
    
    // Initialize file descriptor table
    for (int i = 0; i < MAX_OPEN_FILES; i++) 
    {
        vfs_files[i].inode = NULL;
        vfs_files[i].offset = 0;
        vfs_files[i].flags = 0;
        vfs_files[i].ref_count = 0;
        vfs_files[i].private_data = NULL;
    }
    
    // Register IR0FS filesystem
    vfs_register_filesystem("ir0fs", &ir0fs_ops);
    
    print_success("VFS initialized successfully");
    return 0;
}

vfs_inode_t *vfs_get_inode(const char *path)
{
    if (!path) {
        return NULL;
    }
    
    // Handle root path
    if (strcmp(path, "/") == 0) {
        return &vfs_root;
    }
    
    // Find mount point
    for (int i = 0; i < vfs_mount_count; i++) 
    {
        if (strncmp(path, vfs_mounts[i].path, strlen(vfs_mounts[i].path)) == 0) {
            // TODO: Look up inode in mounted filesystem
            return vfs_mounts[i].inode;
        }
    }
    
    return NULL;
}

int vfs_create_inode(const char *path, vfs_file_type_t type)
{
    if (!path) {
        return -1;
    }
    
    // Find parent directory
    char parent_path[VFS_MAX_PATH];
    char name[VFS_MAX_NAME];
    
    if (vfs_split_path(path, parent_path, name) != 0) {
        return -1;
    }
    
    vfs_inode_t *parent = vfs_get_inode(parent_path);
    if (!parent || parent->type != VFS_INODE_TYPE_DIRECTORY) {
        return -1;
    }
    
    // Find filesystem for parent
    vfs_mount_t *mount = vfs_find_mount(parent_path);
    if (!mount) {
        return -1;
    }
    
    // Create inode using filesystem operations
    if (mount->fs_ops && mount->fs_ops->create_inode) {
        vfs_inode_t *inode;
        int result = mount->fs_ops->create_inode(parent, name, type, &inode);
        if (result == 0 && inode) {
            return 0;
        }
    }
    
    return -1;
}

int vfs_delete_inode(const char *path)
{
    if (!path) {
        return -1;
    }
    
    vfs_inode_t *inode = vfs_get_inode(path);
    if (!inode) {
        return -1;
    }
    
    // Find filesystem
    vfs_mount_t *mount = vfs_find_mount(path);
    if (!mount) {
        return -1;
    }
    
    // Delete inode using filesystem operations
    if (mount->fs_ops && mount->fs_ops->delete_inode) {
        return mount->fs_ops->delete_inode(inode);
    }
    
    return -1;
}

int vfs_open(const char *path, uint32_t flags, vfs_file_t **file)
{
    if (!path || !file) {
        return -1;
    }
    
    // Get or create inode
    vfs_inode_t *inode = vfs_get_inode(path);
    if (!inode) {
        if (flags & VFS_O_CREAT) {
            vfs_file_type_t type = (flags & 0x8000) ? VFS_TYPE_DIRECTORY : VFS_TYPE_REGULAR; // O_DIRECTORY flag
            if (vfs_create_inode(path, type) != 0) {
                return -1;
            }
            inode = vfs_get_inode(path);
            if (!inode) {
                return -1;
            }
        } else {
            return -1;
        }
    }
    
    // Allocate file descriptor
    vfs_file_t *new_file = kmalloc(sizeof(vfs_file_t));
    if (!new_file) {
        return -1;
    }
    
    // Initialize file descriptor
    new_file->inode = inode;
    new_file->offset = 0;
    new_file->flags = flags;
    new_file->ref_count = 1;
    new_file->private_data = NULL;
    
    *file = new_file;
    return 0;
}

int vfs_close(vfs_file_t *file)
{
    if (!file) {
        return -1;
    }
    
    // Decrement reference count
    file->ref_count--;
    
    // If no more references, free the file
    if (file->ref_count == 0) {
        kfree(file);
    }
    
    return 0;
}

ssize_t vfs_read(vfs_file_t *file, void *buf, size_t count)
{
    if (!file || !buf) {
        return -1;
    }
    
    vfs_inode_t *inode = file->inode;
    if (!inode) {
        return -1;
    }
    
    // Check if file is readable
    if (!(file->flags & VFS_O_RDONLY)) {
        return -1;
    }
    
    // Find filesystem
    vfs_mount_t *mount = vfs_find_mount_by_inode(inode);
    if (!mount) {
        return -1;
    }
    
    // Read using filesystem operations
    if (mount->fs_ops && mount->fs_ops->read) {
        ssize_t result = mount->fs_ops->read(file, buf, count);
        if (result > 0) {
            file->offset += result;
        }
        return result;
    }
    
    return -1;
}

ssize_t vfs_write(vfs_file_t *file, const void *buf, size_t count)
{
    if (!file || !buf) {
        return -1;
    }
    
    vfs_inode_t *inode = file->inode;
    if (!inode) {
        return -1;
    }
    
    // Check if file is writable
    if (!(file->flags & VFS_O_WRONLY) && !(file->flags & VFS_O_RDWR)) {
        return -1;
    }
    
    // Find filesystem
    vfs_mount_t *mount = vfs_find_mount_by_inode(inode);
    if (!mount) {
        return -1;
    }
    
    // Write using filesystem operations
    if (mount->fs_ops && mount->fs_ops->write) {
        ssize_t result = mount->fs_ops->write(file, buf, count);
        if (result > 0) {
            file->offset += result;
        }
        return result;
    }
    
    return -1;
}

int vfs_seek(vfs_file_t *file, int64_t offset, vfs_seek_whence_t whence)
{
    if (!file) {
        return -1;
    }
    
    vfs_inode_t *inode = file->inode;
    if (!inode) {
        return -1;
    }
    
    // Calculate new offset
    int64_t new_offset = file->offset;
    switch (whence) {
        case VFS_SEEK_SET:
            new_offset = offset;
            break;
        case VFS_SEEK_CUR:
            new_offset += offset;
            break;
        case VFS_SEEK_END:
            new_offset = inode->size + offset;
            break;
        default:
            return -1;
    }
    
    // Validate offset
    if (new_offset < 0 || new_offset > inode->size) {
        return -1;
    }
    
    file->offset = new_offset;
    return new_offset;
}

int vfs_stat(const char *path, stat_t *statbuf)
{
    if (!path || !statbuf) {
        return -1;
    }
    
    vfs_inode_t *inode = vfs_get_inode(path);
    if (!inode) {
        return -1;
    }
    
    // Fill stat structure
    memset(statbuf, 0, sizeof(stat_t));
    statbuf->st_ino = inode->ino;
    statbuf->st_mode = inode->permissions;
    statbuf->st_uid = inode->uid;
    statbuf->st_gid = inode->gid;
    statbuf->st_size = inode->size;
    statbuf->st_nlink = inode->links;
    statbuf->st_atime = inode->atime;
    statbuf->st_mtime = inode->mtime;
    statbuf->st_ctime = inode->ctime;
    
    // Set file type
    switch (inode->type) {
        case VFS_TYPE_REGULAR:
            statbuf->st_mode |= S_IFREG;
            break;
        case VFS_TYPE_DIRECTORY:
            statbuf->st_mode |= S_IFDIR;
            break;
        case VFS_TYPE_SYMLINK:
            statbuf->st_mode |= S_IFLNK;
            break;
        case VFS_TYPE_CHARDEV:
            statbuf->st_mode |= S_IFCHR;
            break;
        case VFS_TYPE_BLKDEV:
            statbuf->st_mode |= S_IFBLK;
            break;
        case VFS_TYPE_FIFO:
            statbuf->st_mode |= S_IFIFO;
            break;
        case VFS_TYPE_SOCKET:
            statbuf->st_mode |= S_IFSOCK;
            break;
    }
    
    return 0;
}

int vfs_mkdir(const char *path)
{
    if (!path) {
        return -1;
    }
    
    return vfs_create_inode(path, VFS_TYPE_DIRECTORY);
}

int vfs_rmdir(const char *path)
{
    if (!path) {
        return -1;
    }
    
    vfs_inode_t *inode = vfs_get_inode(path);
    if (!inode || inode->type != VFS_INODE_TYPE_DIRECTORY) {
        return -1;
    }
    
    return vfs_delete_inode(path);
}

int vfs_link(const char *oldpath, const char *newpath)
{
    if (!oldpath || !newpath) {
        return -1;
    }
    
    vfs_inode_t *old_inode = vfs_get_inode(oldpath);
    if (!old_inode) {
        return -1;
    }
    
    // Find filesystem
    vfs_mount_t *mount = vfs_find_mount(oldpath);
    if (!mount) {
        return -1;
    }
    
    // Create hard link using filesystem operations
    // TODO: Implement link operation
    print("VFS: link not implemented yet\n");
    
    return -1;
}

int vfs_unlink(const char *path)
{
    if (!path) {
        return -1;
    }
    
    return vfs_delete_inode(path);
}

int vfs_symlink(const char *target, const char *linkpath)
{
    if (!target || !linkpath) {
        return -1;
    }
    
    // Create symlink inode
    if (vfs_create_inode(linkpath, VFS_INODE_TYPE_SYMLINK) != 0) {
        return -1;
    }
    
    // TODO: Store target path in symlink
    return 0;
}

int vfs_readlink(const char *path, char *buf, size_t bufsiz)
{
    if (!path || !buf) {
        return -1;
    }
    
    vfs_inode_t *inode = vfs_get_inode(path);
    if (!inode || inode->type != VFS_INODE_TYPE_SYMLINK) {
        return -1;
    }
    
    // TODO: Read target path from symlink
    strncpy(buf, path, bufsiz - 1);
    buf[bufsiz - 1] = '\0';
    
    return strlen(buf);
}

int vfs_chmod(const char *path, mode_t mode)
{
    if (!path) {
        return -1;
    }
    
    vfs_inode_t *inode = vfs_get_inode(path);
    if (!inode) {
        return -1;
    }
    
    inode->permissions = mode;
    inode->ctime = 0; // TODO: Get current time
    
    return 0;
}

int vfs_chown(const char *path, uid_t owner, gid_t group)
{
    if (!path) {
        return -1;
    }
    
    vfs_inode_t *inode = vfs_get_inode(path);
    if (!inode) {
        return -1;
    }
    
    inode->uid = owner;
    inode->gid = group;
    inode->ctime = 0; // TODO: Get current time
    
    return 0;
}

int vfs_utime(const char *path, const utimbuf_t *times)
{
    if (!path) {
        return -1;
    }
    
    vfs_inode_t *inode = vfs_get_inode(path);
    if (!inode) {
        return -1;
    }
    
    if (times) {
        inode->atime = times->actime;
        inode->mtime = times->modtime;
    } else {
        // TODO: Get current time
        inode->atime = 0;
        inode->mtime = 0;
    }
    inode->ctime = 0; // TODO: Get current time
    
    return 0;
}

int vfs_mount(const char *source, const char *target, const char *fs_type)
{
    if (!source || !target || !fs_type) {
        return -1;
    }
    
    // Check if mount point exists
    vfs_inode_t *mount_inode = vfs_get_inode(target);
    if (!mount_inode || mount_inode->type != VFS_TYPE_DIRECTORY) {
        return -1;
    }
    
    // Find filesystem type
    vfs_filesystem_t *fs = vfs_find_filesystem(fs_type);
    if (!fs) {
        return -1;
    }
    
    // Check if mount point is already mounted
    for (int i = 0; i < vfs_mount_count; i++) {
        if (strcmp(vfs_mounts[i].path, target) == 0) {
            return -1; // Already mounted
        }
    }
    
    // Add mount point
    if (vfs_mount_count >= MAX_MOUNTS) {
        return -1;
    }
    
    vfs_mount_t *mount = &vfs_mounts[vfs_mount_count];
    mount->inode = mount_inode;
    mount->fs_type = fs->type;
    mount->fs_ops = fs->ops;
    mount->flags = 0; // Default flags
    strcpy(mount->path, target);
    strcpy(mount->source, source);
    
    vfs_mount_count++;
    
    return 0;
}

int vfs_umount(const char *target)
{
    if (!target) {
        return -1;
    }
    
    // Find mount point
    for (int i = 0; i < vfs_mount_count; i++) {
        if (strcmp(vfs_mounts[i].path, target) == 0) {
            // Remove mount point
            for (int j = i; j < vfs_mount_count - 1; j++) {
                vfs_mounts[j] = vfs_mounts[j + 1];
            }
            vfs_mount_count--;
            return 0;
        }
    }
    
    return -1;
}





static int vfs_check_permissions(vfs_inode_t *inode, int flags)
{
    // TODO: Implement permission checking
    return 1;
}


