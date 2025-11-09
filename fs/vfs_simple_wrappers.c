// Thin compatibility wrappers for previous vfs_simple API
// These forward to the unified VFS/minix implementations.
#include "vfs.h"
#include "minix_fs.h"
#include <ir0/vga.h>
#include <ir0/stat.h>
#include <string.h>

void vfs_simple_init(void) { vfs_init(); }

int vfs_simple_mkdir(const char *path)
{
    if (!path)
        return -1;
    return vfs_mkdir(path, 0755);
}

int vfs_simple_ls(const char *path)
{
    const char *p = path ? path : "/";
    return vfs_ls(p);
}

int vfs_simple_create_file(const char *path, const char *filename, uint32_t size)
{
    (void)size; // size ignored for simple wrapper
    if (!path || !filename)
        return -1;
    // Build full path
    char full[512];
    if (path[0] == '\0' || strcmp(path, "/") == 0)
    {
        snprintf(full, sizeof(full), "/%s", filename);
    }
    else
    {
        snprintf(full, sizeof(full), "%s/%s", path, filename);
    }
    extern int minix_fs_touch(const char *path, mode_t mode);
    return minix_fs_touch(full, 0644);
}

int vfs_simple_get_directory_count(void)
{
    (void)0;
    return 0;
}

const char *vfs_simple_get_directory_name(int index)
{
    (void)index;
    return NULL;
}

int vfs_file_exists(const char *pathname)
{
    if (!pathname)
        return 0;
    stat_t st;
    if (vfs_stat(pathname, &st) == 0)
        return 1;
    return 0;
}

int vfs_directory_exists(const char *pathname)
{
    if (!pathname)
        return 0;
    stat_t st;
    if (vfs_stat(pathname, &st) == 0)
    {
        return S_ISDIR(st.st_mode) ? 1 : 0;
    }
    return 0;
}

int vfs_allocate_sectors(int count)
{
    (void)count;
    return 0;
}
int vfs_remove_directory(const char *path) { return vfs_rmdir_recursive(path); }
