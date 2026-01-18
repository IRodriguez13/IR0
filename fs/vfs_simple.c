#include "vfs_simple.h"
#include <ir0/vga.h>
#include <string.h>
#include <stddef.h>


#define MAX_DIRECTORIES 100
#define MAX_FILES_PER_DIR 50
#define MAX_FILENAME_LEN 64


typedef struct
{
    char name[MAX_FILENAME_LEN];
    uint32_t size;
    uint32_t permissions;
    uint64_t created_time;
    bool is_directory;
} simple_file_t;


typedef struct
{
    char name[MAX_FILENAME_LEN];
    simple_file_t files[MAX_FILES_PER_DIR];
    int file_count;
    uint32_t permissions;
    uint64_t created_time;
} simple_directory_t;

static simple_directory_t root_directory;
static simple_directory_t directories[MAX_DIRECTORIES];
static int directory_count = 0;
static bool vfs_simple_initialized = false;


static uint64_t get_current_time(void)
{
    static uint64_t fake_time = 1000000;
    return fake_time++;
}

static simple_directory_t *find_directory(const char *path)
{
    if (strcmp(path, "/") == 0)
    {
        return &root_directory;
    }

    for (int i = 0; i < directory_count; i++)
    {
        if (strcmp(directories[i].name, path) == 0)
        {
            return &directories[i];
        }
    }

    return NULL;
}

static bool is_valid_filename(const char *name)
{
    if (!name || strlen(name) == 0 || strlen(name) >= MAX_FILENAME_LEN)
    {
        return false;
    }

    for (int i = 0; name[i]; i++)
    {
        if (name[i] == '/' || name[i] == '\\' || name[i] == ':' || name[i] == '*')
        {
            return false;
        }
    }

    return true;
}


void vfs_simple_init(void)
{
    if (vfs_simple_initialized)
    {
        return;
    }

    strncpy(root_directory.name, "/", MAX_FILENAME_LEN - 1);
    root_directory.name[MAX_FILENAME_LEN - 1] = '\0';
    root_directory.file_count = 0;
    root_directory.permissions = 0755;
    root_directory.created_time = get_current_time();

    simple_file_t system_files[] = {
        {"kernel.log", 1024, 0644, get_current_time(), false},
        {"memory.log", 512, 0644, get_current_time(), false},
        {"recovery.log", 256, 0644, get_current_time(), false}};

    for (int i = 0; i < 3 && root_directory.file_count < MAX_FILES_PER_DIR; i++)
    {
        root_directory.files[root_directory.file_count] = system_files[i];
        root_directory.file_count++;
    }

    directory_count = 0;
    vfs_simple_initialized = true;
}


int vfs_simple_mkdir(const char *path)
{
    if (!vfs_simple_initialized)
    {
        vfs_simple_init();
    }

    if (!path || !is_valid_filename(path))
    {
        return -1;
    }

    if (find_directory(path))
    {
        return -1;
    }

    if (directory_count >= MAX_DIRECTORIES)
    {
        return -1;
    }

    simple_directory_t *new_dir = &directories[directory_count];
    strncpy(new_dir->name, path, MAX_FILENAME_LEN - 1);
    new_dir->name[MAX_FILENAME_LEN - 1] = '\0';
    new_dir->file_count = 0;
    new_dir->permissions = 0755;
    new_dir->created_time = get_current_time();

    directory_count++;

    return 0;
}

int vfs_file_exists(const char *pathname)
{
    if (!pathname)
    {
        return 0;
    }

    for (int i = 0; i < root_directory.file_count; i++)
    {
        if (strcmp(root_directory.files[i].name, pathname) == 0)
        {
            return 1;
        }
    }

    return 0;
}

int vfs_directory_exists(const char *pathname)
{
    if (!pathname)
    {
        return 0;
    }

    if (strcmp(pathname, "/") == 0)
    {
        return 1;
    }

    for (int i = 0; i < directory_count; i++)
    {
        if (strcmp(directories[i].name, pathname) == 0)
        {
            return 1;
        }
    }

    return 0;
}

int vfs_simple_ls(const char *path)
{
    if (!vfs_simple_initialized)
    {
        vfs_simple_init();
    }

    simple_directory_t *dir = find_directory(path);
    if (!dir)
    {
        return -1;
    }

    for (int i = 0; i < dir->file_count; i++)
    {
        simple_file_t *file = &dir->files[i];
        const char *type __attribute__((unused)) = file->is_directory ? "d" : "-";
        const char *permissions __attribute__((unused)) = "rwxr-xr-x";
    }

    for (int i = 0; i < directory_count; i++)
    {
    }

    return 0;
}


int vfs_simple_create_file(const char *path, const char *filename, uint32_t size)
{
    if (!vfs_simple_initialized)
    {
        vfs_simple_init();
    }

    simple_directory_t *dir = find_directory(path);
    if (!dir)
    {
        return -1;
    }

    if (dir->file_count >= MAX_FILES_PER_DIR)
    {
        return -1;
    }

    simple_file_t *new_file = &dir->files[dir->file_count];
    strncpy(new_file->name, filename, MAX_FILENAME_LEN - 1);
    new_file->name[MAX_FILENAME_LEN - 1] = '\0';
    new_file->size = size;
    new_file->permissions = 0644;
    new_file->created_time = get_current_time();
    new_file->is_directory = false;

    dir->file_count++;

    return 0;
}

int vfs_simple_get_directory_count(void)
{
    return directory_count;
}

const char *vfs_simple_get_directory_name(int index)
{
    if (index >= 0 && index < directory_count)
    {
        return directories[index].name;
    }
    return NULL;
}

int vfs_allocate_sectors(int count)
{
    /* Validate input parameters */
    if (count <= 0)
    {
        /* Invalid sector count */
        return -1;
    }
    
    if (count > 1024 * 1024)  /* Max 1GB allocation (512-byte sectors) */
    {
        /* Requested allocation too large */
        return -1;
    }

    /* Minimal implementation: Validate parameters and return success.
     * Full implementation would:
     * - Allocate actual disk sectors for the file
     * - Track allocated sectors in filesystem metadata
     * - Update inode block pointers
     * - Handle fragmentation
     * 
     * Current stub allows basic file operations to proceed without
     * actual disk space allocation (useful for RAMFS-like filesystems)
     */
    return 0;
}

int vfs_remove_directory(const char *path)
{
    /* Validate input parameters */
    if (!path)
    {
        /* NULL path pointer */
        return -1;
    }
    
    if (path[0] == '\0')
    {
        /* Empty path string */
        return -1;
    }
    
    /* Check for root directory - cannot remove */
    if (strcmp(path, "/") == 0)
    {
        return -1; /* Cannot remove root directory */
    }

    /* Minimal implementation: Validate parameters and return success.
     * Full implementation would:
     * - Check if directory exists
     * - Check if directory is empty (no files/subdirectories)
     * - Check permissions (write permission on parent directory)
     * - Remove directory entry from parent
     * - Free directory metadata/inode
     * - Update filesystem structures
     * 
     * Current stub allows basic directory operations to proceed without
     * actual directory removal (useful for simple filesystems or testing)
     */
    return 0;
}
