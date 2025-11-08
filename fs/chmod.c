#include "chmod.h"
#include <fs/vfs.h>
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
        return -1;

    stat_t st;
    if (vfs_stat(path, &st) != 0)
        return -1;

    st.st_mode = (st.st_mode & ~07777) | (mode & 07777);

    // Update inode with new mode
    struct vfs_inode *inode = vfs_path_lookup(path);
    if (!inode)
        return -1;

    inode->i_mode = st.st_mode;

    // Write inode back to disk
    if (inode->i_sb && inode->i_sb->s_op && inode->i_sb->s_op->write_inode)
        return inode->i_sb->s_op->write_inode(inode);

    return 0;
}