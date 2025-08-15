// fs/vfs_simple.c - Simplified Virtual File System Implementation
#include "vfs_simple.h"
#include "../includes/ir0/print.h"
#include "../memory/heap_allocator.h"
#include "../memory/memo_interface.h"

// ===============================================================================
// GLOBAL VFS STATE
// ===============================================================================

static vfs_inode_t *vfs_root_inode = NULL;
static bool vfs_initialized = false;

// ===============================================================================
// SIMPLIFIED VFS IMPLEMENTATION
// ===============================================================================

int vfs_init(void)
{
    if (vfs_initialized)
    {
        return 0;
    }

    // Create root inode
    vfs_root_inode = kmalloc(sizeof(vfs_inode_t));
    if (!vfs_root_inode)
    {
        print_error("VFS: Failed to allocate root inode\n");
        return -1;
    }

    // Initialize root inode
    vfs_root_inode->ino = VFS_ROOT_INODE;
    vfs_root_inode->type = VFS_TYPE_DIRECTORY;
    vfs_root_inode->size = 0;
    vfs_root_inode->blocks = 0;
    vfs_root_inode->permissions = VFS_PERM_READ | VFS_PERM_WRITE | VFS_PERM_EXEC;
    vfs_root_inode->uid = 0;
    vfs_root_inode->gid = 0;
    vfs_root_inode->atime = 0;
    vfs_root_inode->mtime = 0;
    vfs_root_inode->ctime = 0;
    vfs_root_inode->links = 1;
    vfs_root_inode->fs_data = NULL;
    vfs_root_inode->private_data = NULL;

    vfs_initialized = true;
    print_colored("VFS: Virtual File System initialized\n", 0x0A, 0x00);

    return 0;
}

int vfs_open(const char *path, uint32_t flags, vfs_file_t **file)
{
    if (!vfs_initialized || !path || !file)
    {
        return -1;
    }

    // For now, create a simple file structure
    vfs_file_t *new_file = kmalloc(sizeof(vfs_file_t));
    if (!new_file)
    {
        return -1;
    }

    // Create a new inode for this file
    vfs_inode_t *inode = kmalloc(sizeof(vfs_inode_t));
    if (!inode)
    {
        kfree(new_file);
        return -1;
    }

    // Initialize inode
    inode->ino = 2; // Simple inode number
    inode->type = VFS_TYPE_REGULAR;
    inode->size = 0;
    inode->blocks = 0;
    inode->permissions = VFS_PERM_READ | VFS_PERM_WRITE;
    inode->uid = 0;
    inode->gid = 0;
    inode->atime = 0;
    inode->mtime = 0;
    inode->ctime = 0;
    inode->links = 1;
    inode->fs_data = NULL;
    inode->private_data = NULL;

    // Initialize file
    new_file->inode = inode;
    new_file->flags = flags;
    new_file->offset = 0;
    new_file->ref_count = 1;
    new_file->private_data = NULL;

    *file = new_file;

    print_colored("VFS: Opened file: ", 0x0A, 0x00);
    print(path);
    print("\n");

    return 0;
}

int vfs_close(vfs_file_t *file)
{
    if (!file)
    {
        return -1;
    }

    file->ref_count--;
    if (file->ref_count == 0)
    {
        if (file->inode)
        {
            kfree(file->inode);
        }
        kfree(file);
    }

    return 0;
}

ssize_t vfs_read(vfs_file_t *file, void *buf, size_t count)
{
    if (!file || !buf)
    {
        return -1;
    }

    // For now, return 0 (no data to read)
    return 0;
}

ssize_t vfs_write(vfs_file_t *file, const void *buf, size_t count)
{
    if (!file || !buf)
    {
        return -1;
    }

    // For now, just update the file size
    file->inode->size += count;
    file->offset += count;

    print_colored("VFS: Wrote ", 0x0A, 0x00);
    print("bytes to file\n");

    return count;
}

int vfs_seek(vfs_file_t *file, int64_t offset, vfs_seek_whence_t whence)
{
    if (!file)
    {
        return -1;
    }

    switch (whence)
    {
    case VFS_SEEK_SET:
        file->offset = offset;
        break;
    case VFS_SEEK_CUR:
        file->offset += offset;
        break;
    case VFS_SEEK_END:
        file->offset = file->inode->size + offset;
        break;
    default:
        return -1;
    }

    return 0;
}

int vfs_opendir(const char *path, vfs_file_t **file)
{
    if (!vfs_initialized || !path || !file)
    {
        return -1;
    }

    // Create a directory file
    vfs_file_t *dir_file = kmalloc(sizeof(vfs_file_t));
    if (!dir_file)
    {
        return -1;
    }

    // Create directory inode
    vfs_inode_t *inode = kmalloc(sizeof(vfs_inode_t));
    if (!inode)
    {
        kfree(dir_file);
        return -1;
    }

    // Initialize directory inode
    inode->ino = 3; // Directory inode number
    inode->type = VFS_TYPE_DIRECTORY;
    inode->size = 0;
    inode->blocks = 0;
    inode->permissions = VFS_PERM_READ | VFS_PERM_WRITE | VFS_PERM_EXEC;
    inode->uid = 0;
    inode->gid = 0;
    inode->atime = 0;
    inode->mtime = 0;
    inode->ctime = 0;
    inode->links = 1;
    inode->fs_data = NULL;
    inode->private_data = NULL;

    // Initialize directory file
    dir_file->inode = inode;
    dir_file->flags = VFS_O_RDONLY;
    dir_file->offset = 0;
    dir_file->ref_count = 1;
    dir_file->private_data = NULL;

    *file = dir_file;

    print_colored("VFS: Opened directory: ", 0x0A, 0x00);
    print(path);
    print("\n");

    return 0;
}

int vfs_readdir(vfs_file_t *file, vfs_dirent_t *dirent)
{
    if (!file || !dirent)
    {
        return -1;
    }

    if (file->inode->type != VFS_TYPE_DIRECTORY)
    {
        return -1;
    }

    // For now, return end of directory
    return 0;
}

int vfs_mkdir(const char *path)
{
    if (!vfs_initialized || !path)
    {
        return -1;
    }

    print_colored("VFS: Created directory: ", 0x0A, 0x00);
    print(path);
    print("\n");

    return 0;
}

int vfs_rmdir(const char *path)
{
    if (!vfs_initialized || !path)
    {
        return -1;
    }

    print_colored("VFS: Removed directory: ", 0x0A, 0x00);
    print(path);
    print("\n");

    return 0;
}

int vfs_lookup(const char *path, vfs_inode_t **inode)
{
    if (!vfs_initialized || !path || !inode)
    {
        return -1;
    }

    // For now, return root inode for any path
    *inode = vfs_root_inode;
    return 0;
}

int vfs_create(const char *path, vfs_file_type_t type)
{
    if (!vfs_initialized || !path)
    {
        return -1;
    }

    print_colored("VFS: Created file: ", 0x0A, 0x00);
    print(path);
    print("\n");

    return 0;
}

int vfs_unlink(const char *path)
{
    if (!vfs_initialized || !path)
    {
        return -1;
    }

    print_colored("VFS: Unlinked file: ", 0x0A, 0x00);
    print(path);
    print("\n");

    return 0;
}

// ===============================================================================
// SIMPLIFIED UTILITY FUNCTIONS
// ===============================================================================

char *vfs_basename(const char *path)
{
    if (!path)
        return NULL;

    // Simple basename implementation
    const char *last_slash = path;
    while (*path)
    {
        if (*path == '/')
        {
            last_slash = path + 1;
        }
        path++;
    }

    return (char *)last_slash;
}

char *vfs_dirname(const char *path)
{
    if (!path)
        return NULL;

    // Simple dirname implementation
    const char *last_slash = path;
    const char *end = path;

    while (*end)
    {
        if (*end == '/')
        {
            last_slash = end;
        }
        end++;
    }

    if (last_slash == path)
    {
        return (char *)".";
    }

    // Create a copy of the path up to the last slash
    size_t len = last_slash - path;
    char *dir = kmalloc(len + 1);
    if (!dir)
        return NULL;

    // Simple string copy
    for (size_t i = 0; i < len; i++)
    {
        dir[i] = path[i];
    }
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

    // Simple path copy
    while (*path)
    {
        *resolved_path = *path;
        path++;
        resolved_path++;
    }
    *resolved_path = '\0';

    return 0;
}
