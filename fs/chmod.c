#include "chmod.h"
#include <fs/vfs.h>
#include <fs/minix_fs.h>
#include <ir0/kmem.h>
#include <string.h>

/**
 * parse_mode - Convert a chmod symbolic/octal mode string to mode bits
 * @mode_str: Mode string (e.g. "644" or "u+x")
 *
 * Return: Mode bits on success, negative error code on failure
 */
int parse_mode(const char *mode_str)
{
    // Handle octal mode (e.g. "644")
    if (mode_str[0] >= '0' && mode_str[0] <= '7')
    {
        int mode = 0;
        for (int i = 0; mode_str[i]; i++)
        {
            if (mode_str[i] < '0' || mode_str[i] > '7')
                return -1;
            mode = mode * 8 + (mode_str[i] - '0');
        }
        return mode;
    }

    // Handle symbolic mode (e.g. "u+x")
    char who = mode_str[0];
    char op = mode_str[1];
    char perm = mode_str[2];

    // Check who (u/g/o/a)
    if (who != 'u' && who != 'g' && who != 'o' && who != 'a')
        return -1;

    // Check operator (+/-/=)
    if (op != '+' && op != '-' && op != '=')
        return -1;

    // Parse permission (r/w/x)
    int bits = 0;
    switch (perm)
    {
    case 'r':
        bits = (who == 'u' || who == 'a') ? S_IRUSR : 0;
        bits |= (who == 'g' || who == 'a') ? S_IRGRP : 0;
        bits |= (who == 'o' || who == 'a') ? S_IROTH : 0;
        break;
    case 'w':
        bits = (who == 'u' || who == 'a') ? S_IWUSR : 0;
        bits |= (who == 'g' || who == 'a') ? S_IWGRP : 0;
        bits |= (who == 'o' || who == 'a') ? S_IWOTH : 0;
        break;
    case 'x':
        bits = (who == 'u' || who == 'a') ? S_IXUSR : 0;
        bits |= (who == 'g' || who == 'a') ? S_IXGRP : 0;
        bits |= (who == 'o' || who == 'a') ? S_IXOTH : 0;
        break;
    default:
        return -1;
    }

    return bits;
}

/**
 * chmod - Change file access permissions
 * @path: Path to file
 * @mode: New permission bits
 *
 * Return: 0 on success, negative error code on failure
 */
int chmod(const char *path, mode_t mode)
{
    if (!path)
    {
        return -1;
    }

    // Check if MINIX filesystem is available
    extern bool minix_fs_is_working(void);
    if (!minix_fs_is_working())
    {
        return -1;
    }

    // Get current file stats
    stat_t st;
    if (vfs_stat(path, &st) != 0)
    {
        return -1;
    }

    // Get the inode for this path
    minix_inode_t *inode_ptr = minix_fs_find_inode(path);
    if (!inode_ptr)
    {
        return -1;
    }

    // Get the inode number for this path
    uint16_t inode_num = minix_fs_get_inode_number(path);
    if (inode_num == 0)
    {
        return -1;
    }

    // Copy the inode to avoid modifying the cached version
    minix_inode_t inode;
    kmemcpy(&inode, inode_ptr, sizeof(minix_inode_t));

    // Update mode: preserve file type bits, update permission bits
    // File type bits are in the upper bits (IFDIR, IFREG, etc.)
    // Permission bits are in the lower 12 bits (07777)
    uint16_t file_type = inode.i_mode & ~07777;  // Preserve file type
    uint16_t new_perms = (mode & 07777);          // New permissions
    inode.i_mode = file_type | new_perms;

    // Write the updated inode back to disk
    if (minix_fs_write_inode(inode_num, &inode) != 0)
    {
        return -1;
    }

    return 0;
}