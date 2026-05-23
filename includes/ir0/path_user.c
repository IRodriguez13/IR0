// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — userspace path copy + resolve for syscall facades.
 */

#include "path_user.h"
#include "copy_user.h"
#include <ir0/path.h>
#include <ir0/errno.h>
#include <ir0/utimens.h>

int ir0_resolve_kpath_at(int dirfd, const char *path, char *resolved,
                         size_t resolved_sz, const char *cwd)
{
    if (!path || !resolved || resolved_sz == 0)
        return -EINVAL;

    if (path[0] == '\0')
        return -EINVAL;

    if (path[0] == '/')
    {
        if (normalize_path(path, resolved, resolved_sz) != 0)
            return -ENAMETOOLONG;
        return 0;
    }

    if (dirfd != IR0_AT_FDCWD)
        return -EBADF;

    if (!cwd)
        return -EFAULT;

    if (join_paths(cwd, path, resolved, resolved_sz) != 0)
        return -ENAMETOOLONG;

    return 0;
}

int ir0_resolve_user_path_at(int dirfd, const char *user_path, char *resolved,
                             size_t resolved_sz, const char *cwd)
{
    char path_copy[256];

    if (!user_path || !resolved || resolved_sz == 0)
        return -EFAULT;

    if (copy_from_user_cstring(path_copy, sizeof(path_copy), user_path) != 0)
        return -EFAULT;

    return ir0_resolve_kpath_at(dirfd, path_copy, resolved, resolved_sz, cwd);
}

int ir0_resolve_user_path(const char *user_path, char *resolved, size_t resolved_sz,
                          const char *cwd)
{
    return ir0_resolve_user_path_at(IR0_AT_FDCWD, user_path, resolved,
                                    resolved_sz, cwd);
}
