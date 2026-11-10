// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — userspace path copy + resolve for syscall facades.
 */

#include <ir0/path_user.h>
#include <ir0/copy_user.h>
#include <ir0/path.h>
#include <ir0/errno.h>
#include <ir0/utimens.h>

long copy_path_from_user(char *dst, const char *user_path, size_t size)
{
    long len;

    if (!dst || !user_path || size == 0)
        return -EFAULT;

    /* strncpy_from_user() returns its limit when the name has no NUL in it. */
    len = strncpy_from_user(dst, user_path, (long)size);
    if (len < 0)
        return len;
    if ((size_t)len == size)
        return -ENAMETOOLONG;
    return len;
}

int ir0_resolve_kpath_at(int dirfd, const char *path, char *resolved,
                         size_t resolved_sz, const char *cwd,
                         const char *root)
{
    if (!path || !resolved || resolved_sz == 0)
        return -EINVAL;

    /* Linux getname(): an empty name is ENOENT unless AT_EMPTY_PATH. */
    if (path[0] == '\0')
        return -ENOENT;

    if (path[0] == '/')
    {
        if (ir0_path_apply_root(root, path, resolved, resolved_sz) != 0)
            return -ENAMETOOLONG;
        return 0;
    }

    if (dirfd != IR0_AT_FDCWD)
        return -EBADF;

    if (!cwd || cwd[0] != '/')
        cwd = "/";

    if (join_paths(cwd, path, resolved, resolved_sz) != 0)
        return -ENAMETOOLONG;

    return 0;
}

int ir0_resolve_user_path_at(int dirfd, const char *user_path, char *resolved,
                             size_t resolved_sz, const char *cwd,
                             const char *root)
{
    char path_copy[256];
    long len;

    if (!user_path || !resolved || resolved_sz == 0)
        return -EFAULT;

    len = copy_path_from_user(path_copy, user_path, sizeof(path_copy));
    if (len < 0)
        return (int)len;

    return ir0_resolve_kpath_at(dirfd, path_copy, resolved, resolved_sz, cwd,
                                root);
}

int ir0_resolve_user_path(const char *user_path, char *resolved,
                          size_t resolved_sz, const char *cwd,
                          const char *root)
{
    return ir0_resolve_user_path_at(IR0_AT_FDCWD, user_path, resolved,
                                    resolved_sz, cwd, root);
}
