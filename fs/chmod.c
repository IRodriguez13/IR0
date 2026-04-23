#include "chmod.h"
#include <fs/vfs.h>
#include <drivers/serial/serial.h>
#include <errno.h>
#include <string.h>

/*
 * parse_mode - Parse chmod mode string as octal only (e.g. "755", "0644").
 *
 * Symbolic forms like "u+x" are not supported; callers get -EINVAL after a
 * one-line serial notice. Returns non-negative mode bits on success.
 */
int parse_mode(const char *mode_str)
{
    if (!mode_str || mode_str[0] == '\0')
        return -EINVAL;

    if (mode_str[0] >= '0' && mode_str[0] <= '9')
    {
        if (mode_str[0] > '7')
        {
            serial_print("chmod: use octal mode (0-7 digits only), e.g. 755\n");
            return -EINVAL;
        }

        int mode = 0;

        for (int i = 0; mode_str[i]; i++)
        {
            if (mode_str[i] < '0' || mode_str[i] > '7')
            {
                serial_print("chmod: invalid octal mode string\n");
                return -EINVAL;
            }
            mode = mode * 8 + (mode_str[i] - '0');
            if (mode > 07777)
                return -EINVAL;
        }
        return mode;
    }

    serial_print("chmod: symbolic mode not supported; use octal (e.g. 755)\n");
    return -EINVAL;
}

/*
 * chmod - Change file access permissions via VFS for the path's mount.
 * @path: Path to file
 * @mode: New permission bits (lower 12 bits applied by the filesystem)
 *
 * Return: 0 on success, negative errno-style code on failure
 */
int chmod(const char *path, mode_t mode)
{
    if (!path)
        return -EINVAL;

    return vfs_chmod(path, mode);
}
