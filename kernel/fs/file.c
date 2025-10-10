#include <fs/file.h>
#include <process.h>
#include <print.h>
#include <string.h>
#include <errno.h>

// Global file system list
static filesystem_type_t *filesystems = NULL;

// File operations for MINIX
extern file_operations_t minix_file_ops;

int register_filesystem(filesystem_type_t *fs)
{
    if (!fs || !fs->name || !fs->fops)
    {
        return -EINVAL;
    }

    // Add to the beginning of the list
    fs->next = filesystems;
    filesystems = fs;

    return 0;
}

file_descriptor_t *get_file_descriptor(int fd)
{
    if (fd < 0 || fd >= 16 || !current_process)
    {
        return NULL;
    }
    return current_process->fd_table[fd];
}

int alloc_fd(void)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    // Find first available file descriptor (starting from 3)
    for (int i = 3; i < 16; i++)
    {
        if (!current_process->fd_table[i])
        {
            return i;
        }
    }

    return -EMFILE; // Too many open files
}

void free_fd(int fd)
{
    if (fd < 0 || fd >= 16 || !current_process)
    {
        return;
    }

    file_descriptor_t *fdesc = current_process->fd_table[fd];
    if (fdesc)
    {
        fdesc->refcount--;
        if (fdesc->refcount <= 0)
        {
            if (fdesc->close)
            {
                fdesc->close(fdesc);
            }
            kfree(fdesc);
        }
        current_process->fd_table[fd] = NULL;
    }
}

// Initialize the file system
void fs_init(void)
{
    // Register MINIX file system
    static filesystem_type_t minix_fs = {
        .name = "minix",
        .fops = &minix_file_ops,
        .next = NULL};

    if (register_filesystem(&minix_fs) < 0)
    {
        print("Failed to register MINIX file system\n");
    }
}
