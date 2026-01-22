// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: vfs.c
 * Description: Virtual File System abstraction layer with MINIX filesystem
 * integration
 */

#include "vfs.h"
#include "minix_fs.h"
#include "tmpfs.h"
#include <kernel/process.h>
#include <mm/allocator.h>
#include <mm/paging.h>
#include <string.h>
#include <kernel/rr_sched.h>
#include <stdarg.h>
#include <ir0/fcntl.h>
#include <ir0/stat.h>
#include <ir0/permissions.h>
#include <ir0/errno.h>
#include <serial.h>
#include <drivers/serial/serial.h>
#include <ir0/kmem.h>
#include <ir0/vga.h>
#include <kernel/syscalls.h>
#include <drivers/storage/ata.h>

/* Forward declarations and types */
typedef struct
{
  char name[256];
  uint16_t inode;
  uint8_t type;
} vfs_dirent_t;

int vfs_readdir(const char *path, struct vfs_dirent_readdir *entries, int max_entries);
static int build_path(char *dest, size_t dest_size, const char *dir, const char *name);
static int check_directory_path_permissions(const char *path);

[[maybe_unused]] static void format_timestamp(uint32_t timestamp, char *buffer, size_t buffer_size)
{
  if (!buffer || buffer_size < 13)
  {
    return;
  }

  if (timestamp == 0)
  {
    strncpy(buffer, "Jan  1 00:00 ", buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    return;
  }

  const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

  uint32_t days_since_epoch = timestamp / 86400;
  uint32_t seconds_today = timestamp % 86400;
  uint32_t hours = seconds_today / 3600;
  uint32_t minutes = (seconds_today % 3600) / 60;

  uint32_t year = 1970;
  uint32_t month = 0;
  uint32_t day = 1;

  uint32_t days_in_year = 365;
  while (days_since_epoch >= days_in_year)
  {
    days_since_epoch -= days_in_year;
    year++;
    days_in_year = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 366 : 365;
  }

  uint32_t days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))
  {
    days_in_month[1] = 29;
  }

  while (days_since_epoch >= days_in_month[month])
  {
    days_since_epoch -= days_in_month[month];
    month++;
    if (month >= 12)
    {
      month = 0;
      year++;
    }
  }
  day = days_since_epoch + 1;

  buffer[0] = months[month][0];
  buffer[1] = months[month][1];
  buffer[2] = months[month][2];
  buffer[3] = ' ';
  buffer[4] = ' ';
  if (day >= 10)
  {
    buffer[4] = '0' + (day / 10);
  }
  buffer[5] = '0' + (day % 10);
  buffer[6] = ' ';
  buffer[7] = '0' + (hours / 10);
  buffer[8] = '0' + (hours % 10);
  buffer[9] = ':';
  buffer[10] = '0' + (minutes / 10);
  buffer[11] = '0' + (minutes % 10);
  buffer[12] = ' ';
  buffer[13] = '\0';
}

static int build_path(char *dest, size_t dest_size, const char *dir, const char *name)
{
    /* Validate inputs */
    if (!dest || !dir || !name || dest_size == 0)
    {
        return -EINVAL;
    }
    
    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);
    size_t total_len = dir_len + name_len + 2;  /* +2 for '/' and '\0' */
    
    if (total_len > dest_size)
    {
        return -ENAMETOOLONG;  /* Path too long */
    }

  size_t i = 0;

  /* Copy directory */
  for (size_t j = 0; j < dir_len && i < dest_size - 1; j++)
  {
    dest[i++] = dir[j];
  }

  /* Add separator if needed */
  if (dir_len > 0 && dir[dir_len - 1] != '/' && i < dest_size - 1)
  {
    dest[i++] = '/';
  }

  /* Copy filename */
  for (size_t j = 0; j < name_len && i < dest_size - 1; j++)
  {
    dest[i++] = name[j];
  }

  dest[i] = '\0';
  return 0;
}

/* Lista de filesystems registrados */
static struct filesystem_type *filesystems = NULL;

/* Root filesystem */
static struct vfs_superblock *root_sb = NULL;
static struct vfs_inode *root_inode = NULL;

/* Mount points list */
static struct mount_point *mount_points = NULL;

/* Maximum path length */
#define MAX_PATH_LENGTH 256

/* Forward declarations */
static struct filesystem_type *vfs_get_filesystem_for_path(const char *path);

/* Convert open flags to access mode for permission checking */
static int flags_to_access_mode(int flags)
{
    int mode = 0;
    int accmode = flags & O_ACCMODE;
    
    /* Use values directly to avoid conflict between permissions.h and syscall.h */
    if (accmode == O_RDONLY || accmode == O_RDWR)
        mode |= 0x1;  /* Read access */
    
    if (accmode == O_WRONLY || accmode == O_RDWR)
        mode |= 0x2;  /* Write access */
    
    return mode;
}

/* Validate path string - enhanced security checks */
static int validate_path(const char *path)
{
    if (!path)
        return -EINVAL;
    
    size_t len = strlen(path);
    if (len == 0)
        return -EINVAL;
    
    if (len >= MAX_PATH_LENGTH)
        return -ENAMETOOLONG;
    
    /* Security checks: prevent dangerous path patterns */
    const char *p = path;
    int dot_count = 0;
    int slash_count = 0;
    int component_length = 0;
    
    /* Check for consecutive slashes (//) - normalize later but reject now */
    while (*p)
    {
        if (*p == '/')
        {
            slash_count++;
            if (slash_count > 1)
            {
                /* Multiple consecutive slashes - reject for security */
                return -EINVAL;
            }
            /* Reset component length counter */
            component_length = 0;
        }
        else
        {
            slash_count = 0;
            component_length++;
            
            /* Check for excessively long path components */
            if (component_length > 255)
            {
                return -ENAMETOOLONG;
            }
        }
        
        /* Check for parent directory traversal (..) */
        if (*p == '.')
        {
            dot_count++;
            if (dot_count >= 2 && (p[1] == '/' || p[1] == '\0'))
            {
                /* Dangerous: .. found - could traverse outside allowed directories */
                /* Note: We still allow single . for current directory */
                return -EACCES;
            }
        }
        else
        {
            dot_count = 0;
        }
        
        /* Reject control characters, null bytes, and dangerous characters in path */
        if (*p < 0x20 || *p == 0x7F || *p == '\\' || *p == '|' || *p == '<' || *p == '>')
        {
            return -EINVAL;
        }
        
        /* Additional security: reject paths with embedded null sequences */
        if (*p == '\0' && p < path + len - 1)
        {
            return -EINVAL;
        }
        
        p++;
    }
    
    /* Ensure path starts with / (absolute path) or doesn't contain .. at start */
    if (path[0] != '/' && (len >= 2 && path[0] == '.' && path[1] == '.'))
    {
        /* Relative path with .. at start - reject for security */
        return -EACCES;
    }
    
    /* Additional check: reject paths ending with .. */
    if (len >= 2 && path[len-2] == '.' && path[len-1] == '.')
    {
        return -EACCES;
    }
    
    return 0;
}

int register_filesystem(struct filesystem_type *fs)
{
    /* Validate input */
    if (!fs)
        return -EINVAL;
    
    /* Check if filesystem name is valid */
    if (!fs->name || strlen(fs->name) == 0)
        return -EINVAL;
    
    /* Check if already registered */
    struct filesystem_type *current = filesystems;
    while (current)
    {
        if (current == fs || (current->name && strcmp(current->name, fs->name) == 0))
        {
            return -EEXIST;  /* Already registered */
        }
        current = current->next;
    }
    
    /* Register filesystem */
    fs->next = filesystems;
    filesystems = fs;
    return 0;
}

int unregister_filesystem(struct filesystem_type *fs)
{
    /* Validate input */
    if (!fs)
        return -EINVAL;
    
    /* Find and unregister filesystem */
    struct filesystem_type **p = &filesystems;
    while (*p)
    {
        if (*p == fs)
        {
            *p = fs->next;
            return 0;
        }
        p = &(*p)->next;
    }
    
    return -ENOENT;  /* Filesystem not found */
}

/* Find mount point for a given path */
struct mount_point *vfs_find_mount_point(const char *path)
{
  if (!path)
    return NULL;

  struct mount_point *mp = mount_points;
  size_t path_len = strlen(path);
  size_t longest_match = 0;
  struct mount_point *best_match = NULL;

  /* Find longest matching mount point */
  while (mp)
  {
    size_t mp_len = strlen(mp->path);

    /* Check if path starts with mount point path */
    if (strncmp(path, mp->path, mp_len) == 0)
    {
      /* Exact match or path separator after mount point */
      if (mp_len == path_len || path[mp_len] == '/' || mp->path[mp_len - 1] == '/')
      {
        if (mp_len > longest_match)
        {
          longest_match = mp_len;
          best_match = mp;
        }
      }
    }
    mp = mp->next;
  }

  return best_match;
}

/* Add mount point */
int vfs_add_mount_point(const char *path, const char *dev,
                        struct vfs_superblock *sb, struct vfs_inode *root,
                        struct filesystem_type *fs_type)
{
    /* Validate inputs */
    if (!path || !sb || !root || !fs_type)
        return -EINVAL;
    
    /* Validate path */
    int ret = validate_path(path);
    if (ret != 0)
        return ret;
    
    /* Check if mount point already exists */
    if (vfs_find_mount_point(path))
    {
        return -EBUSY;  /* Already mounted */
    }
    
    /* Allocate mount point structure */
    struct mount_point *mp = kmalloc(sizeof(struct mount_point));
    if (!mp)
        return -ENOMEM;

  strncpy(mp->path, path, sizeof(mp->path) - 1);
  mp->path[sizeof(mp->path) - 1] = '\0';

  if (dev)
  {
    strncpy(mp->dev, dev, sizeof(mp->dev) - 1);
    mp->dev[sizeof(mp->dev) - 1] = '\0';
  }
  else
  {
    mp->dev[0] = '\0';
  }

  mp->sb = sb;
  mp->mount_root = root;
  mp->fs_type = fs_type;
  mp->next = mount_points;
  mount_points = mp;

  return 0;
}

/* Remove mount point */
int vfs_remove_mount_point(const char *path)
{
    /* Validate inputs */
    if (!path)
        return -EINVAL;
    
    /* Check if current process exists and is root */
    if (!current_process)
        return -ESRCH;
    
    /* Only root can unmount filesystems */
    if (current_process->uid != ROOT_UID)
        return -EPERM;
    
    /* Find and remove mount point */
    struct mount_point **p = &mount_points;
    while (*p)
    {
        if (strcmp((*p)->path, path) == 0)
        {
            struct mount_point *to_free = *p;
            *p = (*p)->next;
            kfree(to_free);
            return 0;
        }
        p = &(*p)->next;
    }
    
    return -ENOENT;  /* Mount point not found */
}

struct vfs_inode *vfs_path_lookup(const char *path)
{
  if (!path)
    return NULL;

  /* Handle root directory */
  if (strcmp(path, "/") == 0)
  {
    /* If root_inode exists, return it */
    if (root_inode)
      return root_inode;
    
    /* Otherwise, try to find root in mount points */
    struct mount_point *mp = vfs_find_mount_point("/");
    if (mp && mp->mount_root)
      return mp->mount_root;
    
    /* Still no root, return NULL */
    return NULL;
  }
  
  /* For non-root paths, we need root_inode for reference */
  if (!root_inode)
    return NULL;

  /* Check if path is on a mounted filesystem */
  struct mount_point *mp = vfs_find_mount_point(path);

  if (mp)
  {
    /* Path is on a mounted filesystem */
    size_t mp_len = strlen(mp->path);
    const char *remaining_path = path + mp_len;

    /* Skip leading slashes */
    while (*remaining_path == '/')
      remaining_path++;

    /* If path is exactly the mount point, return mount root */
    if (*remaining_path == '\0')
    {
      return mp->mount_root;
    }

    /* Otherwise, use the mounted filesystem's lookup */
    /* Use filesystem-specific lookup operation */
    if (mp->fs_type && mp->fs_type->ops && mp->fs_type->ops->lookup)
    {
      struct vfs_inode *inode = mp->fs_type->ops->lookup(path);
      if (inode)
      {
        inode->i_sb = mp->sb;
        inode->i_op = mp->mount_root ? mp->mount_root->i_op : NULL;
        inode->i_fop = mp->mount_root ? mp->mount_root->i_fop : NULL;
        return inode;
      }
    }
    
    /* Fallback to mount root for filesystems without lookup */

    /* For other filesystems, return mount root as fallback.
     * Full implementation would require filesystem-specific lookup operations.
     */
    return mp->mount_root;
  }

  /* Default: Use root filesystem (MINIX) */
  if (!minix_fs_is_working())
  {
    return NULL;
  }

  /* Find inode using MINIX filesystem */
  uint16_t inode_num = minix_fs_get_inode_number(path);
  if (inode_num == 0)
  {
    return NULL;
  }

  minix_inode_t *minix_inode = minix_fs_find_inode(path);
  if (!minix_inode)
  {
    return NULL;
  }

  /* Convert MINIX inode to VFS inode */
  static struct vfs_inode vfs_inode_wrapper;
  vfs_inode_wrapper.i_ino = inode_num;
  vfs_inode_wrapper.i_mode = minix_inode->i_mode;
  vfs_inode_wrapper.i_size = minix_inode->i_size;
  vfs_inode_wrapper.i_sb = root_inode->i_sb;
  vfs_inode_wrapper.i_private = minix_inode;
  vfs_inode_wrapper.i_op = root_inode->i_op;
  vfs_inode_wrapper.i_fop = root_inode->i_fop;

  return &vfs_inode_wrapper;
}

int vfs_init(void)
{
  /* Inicializar lista de filesystems */
  filesystems = NULL;
  root_sb = NULL;
  root_inode = NULL;
  mount_points = NULL;

  return 0;
}

int vfs_mount(const char *dev, const char *mountpoint, const char *fstype)
{
    /* Validate inputs */
    if (!fstype || !mountpoint)
        return -EINVAL;
    
    /* Check if current process exists and is root (skip during kernel init) */
    if (current_process)
    {
        /* Only root can mount filesystems */
        if (current_process->uid != ROOT_UID)
            return -EPERM;
    }
    /* If current_process is NULL, we're in kernel init and allow mount */
    
    /* Validate mountpoint path */
    int ret = validate_path(mountpoint);
    if (ret != 0)
        return ret;
    
    /* Search for filesystem type */
    struct filesystem_type *fs_type = filesystems;
    while (fs_type)
    {
        if (strcmp(fs_type->name, fstype) == 0)
        {
            break;
        }
        fs_type = fs_type->next;
    }
    
    if (!fs_type)
        return -ENODEV;  /* Filesystem not found */

    /* Montar el filesystem (llama a la función mount específica) */
    int mount_ret = fs_type->mount(dev, mountpoint);
    if (mount_ret != 0)
        return mount_ret;

  if (strcmp(mountpoint, "/") == 0)
  {
    return vfs_add_mount_point("/", dev ? dev : "root", root_sb, root_inode, fs_type);
  }


    /* For filesystems other than root, create mount root inode */
    /* Support MINIX and TMPFS for non-root mounts */
    if (strcmp(fstype, "minix") == 0)
    {
        struct vfs_inode *mount_root = kmalloc(sizeof(struct vfs_inode));
        if (!mount_root)
            return -ENOMEM;
        
        /* Copy structure from root inode */
        memcpy(mount_root, root_inode, sizeof(struct vfs_inode));
        
        return vfs_add_mount_point(mountpoint, dev ? dev : "none", root_sb, mount_root, fs_type);
    }
    else if (strcmp(fstype, "tmpfs") == 0)
    {
        /* TMPFS mount - create mount root inode */
        struct vfs_inode *mount_root = kmalloc(sizeof(struct vfs_inode));
        if (!mount_root)
            return -ENOMEM;
        
        /* Initialize TMPFS mount root inode */
        mount_root->i_ino = 1;  /* TMPFS root is always inode 1 */
        mount_root->i_mode = S_IFDIR | 0755;
        mount_root->i_size = 0;
        mount_root->i_sb = NULL;  /* TMPFS doesn't use superblock in the same way */
        mount_root->i_private = NULL;  /* Will be set by tmpfs lookup */
        mount_root->i_op = NULL;
        mount_root->i_fop = NULL;
        
        return vfs_add_mount_point(mountpoint, dev ? dev : "none", NULL, mount_root, fs_type);
    }
    
    /* For other filesystem types, filesystem-specific mount should handle mount point */
    /* If mount succeeded but no mount point was added, assume it's OK */
    return 0;
}

int vfs_open(const char *path, int flags, struct vfs_file **file)
{
    /* Validate inputs */
    int ret = validate_path(path);
    if (ret != 0)
        return ret;
    
    if (!file)
        return -EFAULT;
    
    /* Check if current process exists */
    if (!current_process)
        return -ESRCH;
    
    /* Check execute permission on all intermediate directories in the path.
     * In Unix, you need execute permission on each directory in the path to
     * access files within them. For example, to access /home/user/file.txt,
     * you need execute permission on /, /home, and /home/user.
     */
    int dir_check = check_directory_path_permissions(path);
    if (dir_check != 0)
        return dir_check;
    
    /* Check permissions before opening */
    int access_mode = flags_to_access_mode(flags);
    
    /* For O_CREAT, check write permission on parent directory */
    if (flags & O_CREAT)
    {
        /* Get parent directory path */
        char parent_path[256];
        const char *last_slash = strrchr(path, '/');
        
        if (last_slash && last_slash != path)
        {
            size_t parent_len = (size_t)(last_slash - path);
            if (parent_len >= sizeof(parent_path))
                return -ENAMETOOLONG;
            
            strncpy(parent_path, path, parent_len);
            parent_path[parent_len] = '\0';
            
            /* Check write permission on parent directory */
            if (!check_file_access(parent_path, 0x2, current_process))  /* ACCESS_WRITE */
                return -EACCES;
        }
    }
    else
    {
        /* Check access permission on file/directory */
        if (!check_file_access(path, access_mode, current_process))
            return -EACCES;
    }
    
    /* Lookup inode */
    struct vfs_inode *inode = vfs_path_lookup(path);
    if (!inode)
    {
        /* File doesn't exist - only OK if O_CREAT is set */
        if (flags & O_CREAT)
        {
            /* Create file in the appropriate filesystem */
            struct filesystem_type *fs = vfs_get_filesystem_for_path(path);
            if (fs && fs->ops && fs->ops->create_file)
            {
                mode_t file_mode = (mode_t)(flags & 0777);  /* Extract mode from flags or use default */
                if (file_mode == 0)
                    file_mode = 0644;  /* Default mode */
                
                int create_ret = fs->ops->create_file(path, file_mode);
                if (create_ret != 0)
                    return create_ret;
                
                /* Lookup the newly created inode */
                inode = vfs_path_lookup(path);
                if (!inode)
                    return -EIO;
            }
            else
            {
                /* For MINIX and other filesystems without create_file, use fallback */
                return -ENOSYS;  /* Creation not supported for this filesystem via open */
            }
        }
        else
        {
            return -ENOENT;
        }
    }
    else if (flags & O_EXCL)
    {
        /* File exists and O_EXCL is set - error */
        return -EEXIST;
    }
    
    /* Allocate file descriptor */
    *file = kmalloc(sizeof(struct vfs_file));
    if (!*file)
        return -ENOMEM;
    
    (*file)->f_inode = inode;
    (*file)->f_pos = 0;
    (*file)->f_flags = flags;
    (*file)->private_data = NULL;
    
    /* Call filesystem-specific open */
    if (inode && inode->i_fop && inode->i_fop->open)
    {
        ret = inode->i_fop->open(inode, *file);
        if (ret != 0)
        {
            kfree(*file);
            *file = NULL;
            return ret;
        }
    }
    
    return 0;
}

int vfs_read(struct vfs_file *file, char *buf, size_t count)
{
    /* Validate inputs */
    if (!file)
        return -EBADF;
    
    if (!buf)
        return -EFAULT;
    
    if (count == 0)
        return 0;
    
    /* Check if file was opened for reading */
    int accmode = file->f_flags & O_ACCMODE;
    if (accmode == O_WRONLY)
        return -EBADF;
    
    /* Call filesystem-specific read */
    if (file->f_inode->i_fop && file->f_inode->i_fop->read)
    {
        return file->f_inode->i_fop->read(file, buf, count);
    }
    
    return -ENOSYS;
}

int vfs_write(struct vfs_file *file, const char *buf, size_t count)
{
    /* Validate inputs */
    if (!file)
        return -EBADF;
    
    if (!buf)
        return -EFAULT;
    
    if (count == 0)
        return 0;
    
    /* Check if file was opened for writing */
    int accmode = file->f_flags & O_ACCMODE;
    if (accmode == O_RDONLY)
        return -EBADF;
    
    /* Check write permission */
    if (!current_process)
        return -ESRCH;
    
    /* Validate write permissions:
     * 1. File must have been opened with write flags (O_WRONLY or O_RDWR)
     * 2. Inode must allow write access
     * 3. Process must have write permission
     */
    
    /* Check if file was opened with write flags */
    if (!(file->f_flags & (O_WRONLY | O_RDWR)))
    {
        return -EBADF; /* File not opened for writing */
    }
    
    /* Check inode permissions - at minimum, inode should exist */
    if (!file->f_inode)
    {
        return -EINVAL;
    }
    
    /* Note: Full permission checking would require:
     * - Storing file path in vfs_file structure
     * - Checking file system permissions (mode bits)
     * - Checking process UID/GID against file UID/GID
     * For now, we validate basic flags and inode existence
     */
    
    /* Call filesystem-specific write */
    if (file->f_inode->i_fop && file->f_inode->i_fop->write)
    {
        return file->f_inode->i_fop->write(file, buf, count);
    }
    
    return -ENOSYS;
}

int vfs_append(const char *path, const char *buf, size_t count)
{
    /* Validate inputs */
    int ret = validate_path(path);
    if (ret != 0)
        return ret;
    
    if (!buf)
        return -EFAULT;
    
    if (count == 0)
        return 0;
    
    /* Open file with append mode (vfs_open will check permissions) */
    struct vfs_file *file;
    ret = vfs_open(path, O_WRONLY | O_APPEND, &file);
    if (ret != 0)
        return ret;
    
    /* Move to end of file */
    if (file->f_inode->i_fop && file->f_inode->i_fop->seek)
    {
        file->f_inode->i_fop->seek(file, 0, SEEK_END);
    }
    else
    {
        file->f_pos = file->f_inode->i_size;
    }
    
    /* Write data */
    ret = vfs_write(file, buf, count);
    
    vfs_close(file);
    return ret;
}

int vfs_close(struct vfs_file *file)
{
    if (!file)
        return -EBADF;
    
    int ret = 0;
    if (file->f_inode->i_fop && file->f_inode->i_fop->close)
    {
        ret = file->f_inode->i_fop->close(file);
    }
    
    kfree(file);
    return ret;
}

int vfs_ls(const char *path)
{
    /* Validate input */
    int ret = validate_path(path);
    if (ret != 0)
        return ret;
    
    /* Get filesystem for this path */
    struct filesystem_type *fs = vfs_get_filesystem_for_path(path);
    
    /* Use filesystem-specific ls operation if available */
    if (fs && fs->ops && fs->ops->ls)
    {
        return fs->ops->ls(path, false);
    }
    
    /* Fallback: try MINIX if available */
    if (minix_fs_is_working())
    {
        return minix_fs_ls(path, false);
    }
    
    return -ENODEV;  /* No filesystem available */
}

int vfs_mkdir(const char *path, int mode)
{
    /* Validate inputs */
    int ret = validate_path(path);
    if (ret != 0)
        return ret;
    
    /* Check if current process exists */
    if (!current_process)
        return -ESRCH;
    
    /* Check write permission on parent directory */
    char parent_path[256];
    const char *last_slash = strrchr(path, '/');
    
    if (last_slash && last_slash != path)
    {
        size_t parent_len = (size_t)(last_slash - path);
        if (parent_len >= sizeof(parent_path))
            return -ENAMETOOLONG;
        
        strncpy(parent_path, path, parent_len);
        parent_path[parent_len] = '\0';
        
        /* Check write permission on parent directory */
        if (!check_file_access(parent_path, 0x2, current_process))  /* ACCESS_WRITE */
            return -EACCES;
    }
    else if (strcmp(path, "/") == 0)
    {
        /* Cannot create root directory */
        return -EEXIST;
    }
    
    /* Check if directory already exists */
    stat_t st;
    if (vfs_stat(path, &st) == 0)
    {
        if (S_ISDIR(st.st_mode))
            return -EEXIST;
        else
            return -ENOTDIR;
    }
    
    /* Delegate to filesystem-specific implementation */
    struct filesystem_type *fs = vfs_get_filesystem_for_path(path);
    if (fs && fs->ops && fs->ops->mkdir)
    {
        return fs->ops->mkdir(path, (mode_t)mode);
    }
    
    /* Fallback to MINIX */
    return minix_fs_mkdir(path, (mode_t)mode);
}

int vfs_unlink(const char *path)
{
    /* Validate inputs */
    int ret = validate_path(path);
    if (ret != 0)
        return ret;
    
    /* Check if current process exists */
    if (!current_process)
        return -ESRCH;
    
    /* Cannot unlink root directory */
    if (strcmp(path, "/") == 0)
        return -EPERM;
    
    /* Get file stats to check permissions */
    stat_t st;
    if (vfs_stat(path, &st) != 0)
        return -ENOENT;
    
    /* Check write permission */
    if (S_ISDIR(st.st_mode))
    {
        /* For directories, check write permission on parent */
        char parent_path[256];
        const char *last_slash = strrchr(path, '/');
        
        if (last_slash && last_slash != path)
        {
            size_t parent_len = (size_t)(last_slash - path);
            if (parent_len >= sizeof(parent_path))
                return -ENAMETOOLONG;
            
            strncpy(parent_path, path, parent_len);
            parent_path[parent_len] = '\0';
            
            if (!check_file_access(parent_path, 0x2, current_process))  /* ACCESS_WRITE */
                return -EACCES;
        }
    }
    else
    {
        /* For files, check write permission on the file itself */
        if (!check_file_access(path, 0x2, current_process))  /* ACCESS_WRITE */
            return -EACCES;
    }
    
    /* Delegate to filesystem-specific implementation */
    struct filesystem_type *fs = vfs_get_filesystem_for_path(path);
    if (fs && fs->ops && fs->ops->unlink)
    {
        return fs->ops->unlink(path);
    }
    
    /* Fallback to MINIX */
    return minix_fs_rm(path);
}

int vfs_link(const char *oldpath, const char *newpath)
{
    /* Validate inputs */
    int ret = validate_path(oldpath);
    if (ret != 0)
        return ret;
    
    ret = validate_path(newpath);
    if (ret != 0)
        return ret;
    
    /* Check if current process exists */
    if (!current_process)
        return -ESRCH;
    
    /* Check that oldpath exists and is readable */
    stat_t st;
    if (vfs_stat(oldpath, &st) != 0)
        return -ENOENT;
    
    if (!check_file_access(oldpath, 0x1, current_process))  /* ACCESS_READ */
        return -EACCES;
    
    /* Check write permission on parent directory of newpath */
    char parent_path[256];
    const char *last_slash = strrchr(newpath, '/');
    
    if (last_slash && last_slash != newpath)
    {
        size_t parent_len = (size_t)(last_slash - newpath);
        if (parent_len >= sizeof(parent_path))
            return -ENAMETOOLONG;
        
        strncpy(parent_path, newpath, parent_len);
        parent_path[parent_len] = '\0';
        
        /* Check write permission on parent directory */
        if (!check_file_access(parent_path, 0x2, current_process))  /* ACCESS_WRITE */
            return -EACCES;
    }
    
    /* Check if newpath already exists */
    if (vfs_stat(newpath, &st) == 0)
        return -EEXIST;
    
    /* Delegate to filesystem-specific implementation */
    return minix_fs_link(oldpath, newpath);
}

/* Internal recursive function with depth limit to prevent stack overflow */
static int vfs_rmdir_recursive_internal(const char *path, int depth)
{
    
    /* Limit recursion depth to prevent stack overflow (max 32 levels) */
    if (depth > 32)
    {
        sys_write(2, "rm: recursion depth limit exceeded\n", 35);
        return -ELOOP;
    }
    
    /* Validate path */
    int ret = validate_path(path);
    if (ret != 0)
        return ret;
    
    /* Normalize path: ensure it starts with / */
    char normalized_path[256];  /* Reduced from 512 to prevent stack overflow */
    if (path[0] != '/')
    {
        normalized_path[0] = '/';
        size_t len = strlen(path);
        if (len >= sizeof(normalized_path) - 1)
        {
            return -ENAMETOOLONG;  /* Path too long */
        }
        strncpy(normalized_path + 1, path, sizeof(normalized_path) - 2);
        normalized_path[sizeof(normalized_path) - 1] = '\0';
    }
    else
    {
        size_t len = strlen(path);
        if (len >= sizeof(normalized_path))
        {
            return -ENAMETOOLONG;  /* Path too long */
        }
        strncpy(normalized_path, path, sizeof(normalized_path) - 1);
        normalized_path[sizeof(normalized_path) - 1] = '\0';
    }
    
    /* Check if path is valid and not root */
    if (normalized_path[0] == '\0' || (normalized_path[0] == '/' && normalized_path[1] == '\0'))
    {
        sys_write(2, "rm: cannot remove root directory\n", 33);
        return -EPERM;
    }
    
    /* First check if it's a directory */
    stat_t st;
    if (vfs_stat(normalized_path, &st) != 0)
    {
        /* File doesn't exist or error reading stat */
        return -ENOENT;
    }

  if (!S_ISDIR(st.st_mode))
  {
    /* Not a directory, try to remove as file */
    return vfs_unlink(normalized_path);
  }

  /* Read directory contents (reduced buffer size) */
  struct vfs_dirent_readdir entries[32]; // Reduced from 64 to prevent stack overflow
  int entry_count = vfs_readdir(normalized_path, entries, 32);

    if (entry_count < 0)
    {
        /* Error reading directory, but it exists - try to remove it anyway */
        /* This might happen if directory is already empty or corrupted */
        return minix_fs_rmdir(normalized_path);
    }

  /* Recursively delete all entries */
  for (int i = 0; i < entry_count; i++)
  {
    /* Skip . and .. - check both name and first character */
    if (entries[i].name[0] == '\0')
    {
      continue;
    }

    if (entries[i].name[0] == '.' &&
        (entries[i].name[1] == '\0' ||
         (entries[i].name[1] == '.' && entries[i].name[2] == '\0')))
    {
      continue;
    }

    /* Build full path (reduced buffer size) */
    char full_path[256]; // Reduced from 512 to prevent stack overflow
    if (build_path(full_path, sizeof(full_path), normalized_path, entries[i].name) != 0)
    {
      continue; // Skip if path too long
    }

    /* Prevent infinite recursion - check if we're trying to delete parent */
    if (strcmp(full_path, normalized_path) == 0)
    {
      continue;
    }

    /* Check if it's a directory */
    stat_t entry_st;
    if (vfs_stat(full_path, &entry_st) == 0)
    {
      if (S_ISDIR(entry_st.st_mode))
      {
        /* Recursively delete subdirectory with increased depth */
        if (vfs_rmdir_recursive_internal(full_path, depth + 1) != 0)
        {
          /* Don't fail completely, just log and continue
           * Some entries might have been deleted already
           */
          continue;
        }
      }
      else
      {
        /* Delete file */
        if (vfs_unlink(full_path) != 0)
        {
          /* Don't fail completely, just log and continue */
          continue;
        }
      }
    }
  }

  /* Finally, remove the now-empty directory */
  return minix_fs_rmdir(normalized_path);
}

int vfs_rmdir_recursive(const char *path)
{
  /* Start recursion with depth 0 */
  return vfs_rmdir_recursive_internal(path, 0);
}

/* Get filesystem type for a given path */
static struct filesystem_type *vfs_get_filesystem_for_path(const char *path)
{
    if (!path)
        return NULL;
    
    /* Check if path is on a mounted filesystem */
    struct mount_point *mp = vfs_find_mount_point(path);
    if (mp && mp->fs_type)
    {
        return mp->fs_type;
    }
    
    /* Default to first registered filesystem (typically MINIX for root) */
    return filesystems;
}

int vfs_stat(const char *path, stat_t *buf)
{
    /* Validate inputs */
    if (!path || !buf)
    {
        return -EINVAL;
    }
    
    /* Validate path */
    int ret = validate_path(path);
    if (ret != 0)
        return ret;
    
    /* Get filesystem for this path */
    struct filesystem_type *fs = vfs_get_filesystem_for_path(path);
    
    /* Route stat request through VFS layer to appropriate filesystem */
    if (fs && fs->ops && fs->ops->stat)
    {
        return fs->ops->stat(path, buf);
    }
    
    /* Fallback: try MINIX if available */
    if (minix_fs_is_working())
    {
        return minix_fs_stat(path, buf);
    }
    
    return -ENODEV;  /* No filesystem available */
}

int vfs_readdir(const char *path, struct vfs_dirent_readdir *entries, int max_entries)
{
    /* Validate inputs */
    if (!path || !entries || max_entries <= 0)
    {
        return -EINVAL;
    }
    
    /* Validate path */
    int ret = validate_path(path);
    if (ret != 0)
        return ret;
    
    /* Check if current process exists */
    if (!current_process)
        return -ESRCH;
    
    /* Check execute permission on directory before reading it.
     * In Unix, you need execute permission on a directory to:
     * - Enter the directory (cd)
     * - Access files within it (even if you have read permission)
     * - List its contents (readdir)
     */
    if (!check_file_access(path, ACCESS_EXEC, current_process))
    {
        return -EACCES;  /* Permission denied - no execute permission on directory */
    }
    
    /* Get filesystem for this path */
    struct filesystem_type *fs = vfs_get_filesystem_for_path(path);
    
    /* Use filesystem-specific readdir if available */
    if (fs && fs->ops && fs->ops->readdir)
    {
        return fs->ops->readdir(path, entries, max_entries);
    }
    
    /* Fallback to MINIX if available */
    if (!minix_fs_is_working())
    {
        return -ENODEV;  /* Filesystem not available */
    }

    /* Get directory inode */
    minix_inode_t *dir_inode = minix_fs_find_inode(path);
    if (!dir_inode)
    {
        return -ENOENT;  /* Path not found */
    }
    
    if (!minix_is_dir(dir_inode))
    {
        return -ENOTDIR;  /* Not a directory */
    }

  int entry_count = 0;

  for (int i = 0; i < 7 && entry_count < max_entries; i++)
  {
    if (dir_inode->i_zone[i] == 0)
    {
      continue;
    }

    uint8_t block_buffer[MINIX_BLOCK_SIZE];
    if (minix_read_block(dir_inode->i_zone[i], block_buffer) != 0)
    {
      continue;
    }

    typedef struct
    {
      uint16_t inode;
      char name[14];
    } minix_dir_entry_t;

    minix_dir_entry_t *minix_entries = (minix_dir_entry_t *)block_buffer;
    int num_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);

    for (int j = 0; j < num_entries && entry_count < max_entries; j++)
    {
      if (minix_entries[j].inode == 0)
      
        continue;
      

      strncpy(entries[entry_count].name, minix_entries[j].name, sizeof(entries[entry_count].name) - 1);
      entries[entry_count].name[sizeof(entries[entry_count].name) - 1] = '\0';
      entries[entry_count].inode = minix_entries[j].inode;
      entries[entry_count].type = 0;
      entry_count++;
    }
  }

  return entry_count;
}

/**
 * check_directory_path_permissions - Check execute permission on all intermediate directories
 * @path: Full path to file/directory (e.g., "/home/user/file.txt")
 *
 * In Unix, to access a file, you need execute permission on every directory
 * in the path. For example, to access /home/user/file.txt, you need execute
 * permission on /, /home, and /home/user.
 *
 * Returns: 0 on success, negative error code on failure
 */
static int check_directory_path_permissions(const char *path)
{
    if (!path || !current_process)
        return -EINVAL;
    
    /* Root can access everything */
    if (is_root(current_process))
        return 0;
    
    /* Handle root directory - always accessible */
    if (strcmp(path, "/") == 0)
        return 0;
    
    /* Build path components and check each directory */
    char dir_path[256];
    size_t path_len = strlen(path);
    
    if (path_len >= sizeof(dir_path))
        return -ENAMETOOLONG;
    
    /* Start from root */
    strncpy(dir_path, "/", sizeof(dir_path) - 1);
    dir_path[sizeof(dir_path) - 1] = '\0';
    
    /* Skip leading slash */
    const char *p = path + 1;
    
    /* Process each component */
    while (*p)
    {
        /* Find next slash or end of string */
        const char *next_slash = strchr(p, '/');
        
        if (next_slash)
        {
            /* Component is a directory - check permission */
            size_t component_len = (size_t)(next_slash - p);
            size_t dir_path_len = strlen(dir_path);
            
            /* Build full directory path */
            if (dir_path_len + 1 + component_len >= sizeof(dir_path))
                return -ENAMETOOLONG;
            
            /* Add component to path (unless root) */
            if (dir_path_len > 1 && dir_path[dir_path_len - 1] != '/')
                strncat(dir_path, "/", sizeof(dir_path) - strlen(dir_path) - 1);
            
            strncat(dir_path, p, component_len);
            dir_path[sizeof(dir_path) - 1] = '\0';
            
            /* Check execute permission on this directory */
            if (!check_file_access(dir_path, ACCESS_EXEC, current_process))
            {
                return -EACCES;  /* Permission denied - no execute permission on directory */
            }
            
            /* Move to next component */
            p = next_slash + 1;
        }
        else
        {
            /* Last component (file or final directory) - we've already checked all parent dirs */
            break;
        }
    }
    
    return 0;
}

int vfs_ls_with_stat(const char *path)
{
    /* Validate input */
    int ret = validate_path(path);
    if (ret != 0)
        return ret;
    
    /* Get filesystem for this path */
    struct filesystem_type *fs = vfs_get_filesystem_for_path(path);
    
    /* Use filesystem-specific ls operation if available */
    if (fs && fs->ops && fs->ops->ls)
    {
        return fs->ops->ls(path, true);
    }
    
    /* Fallback: try MINIX if available */
    if (minix_fs_is_working())
    {
        return minix_fs_ls(path, true);
    }
    
    return -ENODEV;  /* No filesystem available */
}

/* Forward declarations */
static int minix_mount(const char *dev_name, const char *dir_name);
static struct vfs_inode *minix_lookup_wrapper(const char *path);

/**
 * minix_lookup_wrapper - Convert MINIX lookup result to VFS inode
 * @path: Path to look up
 * 
 * Wrapper function that converts MINIX filesystem lookup result
 * into a VFS inode structure.
 * 
 * Returns: VFS inode on success, NULL on failure
 */
static struct vfs_inode *minix_lookup_wrapper(const char *path)
{
    static struct vfs_inode vfs_inode_wrapper;
    minix_inode_t *minix_inode = minix_fs_find_inode(path);
    if (!minix_inode)
        return NULL;
    
    vfs_inode_wrapper.i_ino = minix_fs_get_inode_number(path);
    vfs_inode_wrapper.i_mode = minix_inode->i_mode;
    vfs_inode_wrapper.i_size = minix_inode->i_size;
    vfs_inode_wrapper.i_private = minix_inode;
    return &vfs_inode_wrapper;
}

/**
 * MINIX filesystem operations
 * 
 * Implements filesystem_operations interface for MINIX filesystem.
 * All operations are path-based, decoupling VFS from MINIX internals.
 */
static int minix_fs_read_file_wrapper(const char *path, void *buf, size_t count, size_t *read_count, off_t offset)
{
    void *data = NULL;
    size_t size = 0;
    int ret = minix_fs_read_file(path, &data, &size);
    if (ret != 0 || !data)
        return ret;
    
    /* Copy data respecting offset */
    if (offset < 0 || (size_t)offset >= size)
    {
        if (read_count) *read_count = 0;
        kfree(data);
        return 0;  /* EOF */
    }
    
    size_t available = size - (size_t)offset;
    size_t to_read = (count < available) ? count : available;
    memcpy(buf, (uint8_t *)data + offset, to_read);
    
    if (read_count) *read_count = to_read;
    kfree(data);
    return 0;
}

static int minix_fs_write_file_wrapper(const char *path, const void *buf, size_t count, size_t *written_count, off_t offset)
{
    /* MINIX write is simple - write entire buffer */
    (void)offset;  /* MINIX doesn't support offset writes via this interface yet */
    const char *str = (const char *)buf;
    int ret = minix_fs_write_file(path, str);
    if (ret == 0 && written_count)
        *written_count = count;
    return ret;
}

static struct filesystem_operations minix_fs_ops = {
    .stat = minix_fs_stat,
    .mkdir = minix_fs_mkdir,
    .create_file = minix_fs_touch,
    .unlink = minix_fs_rm,
    .rmdir = minix_fs_rmdir,
    .readdir = NULL,  /* MINIX uses minix_fs_ls internally */
    .read_file = minix_fs_read_file_wrapper,
    .write_file = minix_fs_write_file_wrapper,
    .lookup = minix_lookup_wrapper,
    .get_inode_number = (uint32_t (*)(const char *))minix_fs_get_inode_number,
    .ls = minix_fs_ls,
    .link = minix_fs_link,
    .is_available = minix_fs_is_available,
    .is_working = minix_fs_is_working,
};

/**
 * TMPFS filesystem operations
 * 
 * Note: TMPFS filesystem operations are defined in tmpfs.c
 * as they need to be exported for registration.
 */

/* Operaciones de archivo para MINIX - Implementadas via syscalls */
static struct file_operations minix_file_ops = {
    .open = NULL,  /* Implementado via sys_open */
    .read = NULL,  /* Implementado via sys_read */
    .write = NULL, /* Implementado via sys_write */
    .close = NULL, /* Implementado via sys_close */
};

/* Operaciones de inode para MINIX - Implementadas via syscalls */
static struct inode_operations minix_inode_ops = {
    .lookup = NULL, /* Implementado via minix_fs_find_inode */
    .create = NULL, /* Implementado via sys_creat/sys_touch */
    .mkdir = NULL,  /* Implementado via sys_mkdir */
    .unlink = NULL, /* Implementado via sys_unlink */
};

/* Operaciones de superblock para MINIX - Implementadas via MINIX FS */
static struct super_operations minix_super_ops = {
    .read_inode = NULL,   /* Implementado via minix_fs_find_inode */
    .write_inode = NULL,  /* Implementado via minix_fs_write_file */
    .delete_inode = NULL, /* Implementado via minix_fs_rm */
};

/* MINIX filesystem type definition */
static struct filesystem_type minix_fs_type = {
    .name = "minix",
    .mount = minix_mount,
    .ops = &minix_fs_ops,
    .next = NULL
};

/* Mount function para MINIX */
static int minix_mount(const char *dev_name __attribute__((unused)), const char *dir_name __attribute__((unused)))
{

  /* Inicializar MINIX filesystem si no está funcionando */
  if (!minix_fs_is_working())
  {
    int ret = minix_fs_init();
    if (ret != 0)
    {
      serial_print("[VFS] ERROR - MINIX_MOUNT: minix_fs_init failed with error code: ");
      {
        serial_print_hex32((uint32_t)ret);
        serial_print("\n");
      }
      return ret;
    }
    print("MINIX_MOUNT: minix_fs_init OK\n");
  }
  else
  {
    print("MINIX_MOUNT: MINIX FS already working\n");
  }

  /* Crear superblock si no existe */
  if (!root_sb)
  {
    print("MINIX_MOUNT: Creating superblock...\n");
    root_sb = kmalloc(sizeof(struct vfs_superblock));
    if (!root_sb)
    {
      print("MINIX_MOUNT: ERROR - kmalloc failed for superblock\n");
      serial_print("[VFS] ERROR - MINIX_MOUNT: kmalloc failed for superblock (out of memory)\n");
      return -ENOMEM;
    }

    root_sb->s_op = &minix_super_ops;
    root_sb->s_type = &minix_fs_type; /* Asignar el tipo correcto */
    root_sb->s_fs_info = NULL;        /* Datos específicos de MINIX */
    print("MINIX_MOUNT: Superblock created OK\n");
  }
  else
  {
    print("MINIX_MOUNT: Superblock already exists\n");
  }

  /* Crear root inode si no existe */
  if (!root_inode)
  {
    print("MINIX_MOUNT: Creating root inode...\n");
    root_inode = kmalloc(sizeof(struct vfs_inode));
    if (!root_inode)
    {
      print("MINIX_MOUNT: ERROR - kmalloc failed for root_inode\n");
      serial_print("[VFS] ERROR - MINIX_MOUNT: kmalloc failed for root_inode (out of memory)\n");
      kfree(root_sb);
      root_sb = NULL;
      return -ENOMEM;
    }

    root_inode->i_ino = 1;               /* Root inode number */
    root_inode->i_mode = 0040755;        /* Directory with 755 permissions */
    root_inode->i_size = 0;              /* Directory size */
    root_inode->i_op = &minix_inode_ops; /* Inode operations */
    root_inode->i_fop = &minix_file_ops; /* File operations */
    root_inode->i_sb = root_sb;          /* Superblock reference */
    root_inode->i_private = NULL;        /* No private data */

    serial_print("MINIX_MOUNT: Root inode CREATED SUCCESSFULLY\n");
  }
  else
  {
    serial_print("MINIX_MOUNT: Root inode already exists\n");
  }

  print("MINIX_MOUNT: Mount completed successfully\n");
  return 0;
}

/* Removed duplicate minix_fs_type definition */

/* Initialize VFS with MINIX filesystem */
int vfs_init_with_minix(void)
{

  /* Inicializar VFS */
  print("VFS: Initializing VFS...\n");
  int ret = vfs_init();
  if (ret != 0)
  {
    print("VFS: ERROR - vfs_init failed\n");
    serial_print("[VFS] ERROR - vfs_init failed with error code: ");
    {
      serial_print_hex32((uint32_t)ret);
      serial_print("\n");
    }
    return ret;
  }
  print("VFS: vfs_init OK\n");

  /* Registrar MINIX filesystem */
  print("VFS: Registering MINIX filesystem...\n");
  ret = register_filesystem(&minix_fs_type);
  if (ret != 0)
  {
    print("VFS: ERROR - register_filesystem failed\n");
    serial_print("[VFS] ERROR - register_filesystem failed with error code: ");
    {
      serial_print_hex32((uint32_t)ret);
      serial_print("\n");
    }
    return ret;
  }
  print("VFS: register_filesystem OK\n");

  /* Registrar TMPFS filesystem */
  print("VFS: Registering TMPFS filesystem...\n");
  ret = tmpfs_register();
  if (ret != 0)
  {
    print("VFS: WARNING - tmpfs_register failed (non-critical)\n");
    serial_print("[VFS] WARNING - tmpfs_register failed\n");
    /* Don't fail initialization if TMPFS registration fails */
  }
  else
  {
    print("VFS: TMPFS registered OK\n");
  }

  /* Check if storage is available before mounting */
  
  if (!ata_is_available())
  {
    print("VFS: ERROR - No ATA storage available\n");
    print("VFS: Cannot mount root filesystem\n");
    serial_print("[VFS] ERROR - No ATA storage available, cannot mount root filesystem\n");
    return -ENODEV;
  }
  
  /* Check if first drive (hda) is present */
  if (!ata_drive_present(0))
  {
    print("VFS: WARNING - Drive 0 (/dev/hda) not present\n");
    print("VFS: Attempting mount anyway (may fail)\n");
    serial_print("[VFS] WARNING - Drive 0 (/dev/hda) not present, attempting mount anyway\n");
  }
  else
  {
    print("VFS: Drive 0 (/dev/hda) detected\n");
  }

  /* Montar root filesystem */
  print("VFS: Mounting root filesystem...\n");
  ret = vfs_mount("/dev/hda", "/", "minix");
  if (ret != 0)
  {
    print("VFS: ERROR - vfs_mount failed\n");
    serial_print("[VFS] vfs_mount returned error code: ");
    {
      serial_print_hex32((uint32_t)ret);
      serial_print("\n");
    }
    return ret;
  }
  print("VFS: vfs_mount OK\n");

  /* Verificar que root_inode se creó */
  if (root_inode)
  {
    print("VFS: root_inode created successfully\n");
  }
  else
  {
    print("VFS: ERROR - root_inode is still NULL\n");
    serial_print("[VFS] ERROR - root_inode is still NULL after mount attempt\n");
    return -ENODEV;  /* Root inode not created */
  }

  /* Agregar root mount point */
  ret = vfs_add_mount_point("/", "/dev/hda", root_sb, root_inode, &minix_fs_type);
  if (ret != 0)
  {
    print("VFS: WARNING - Could not add root mount point\n");
    serial_print("[VFS] WARNING - Could not add root mount point, error code: ");
    {
      serial_print_hex32((uint32_t)ret);
      serial_print("\n");
    }
  }

  return 0;
}

/**
 * Read entire file into memory - for ELF loader
 * This is a utility function that reads a complete file into a buffer
 */
int vfs_read_file(const char *path, void **data, size_t *size)
{
  if (!path || !data || !size)
  {
    return -1;
  }

  /* VFS layer implementation - route to appropriate filesystem
   * Determine filesystem type based on path or mount table
   */

  /* Check if path starts with root */
  if (path[0] != '/')
  {
    return -1; /* Invalid path */
  }

  /* Route to MINIX filesystem (primary filesystem) */
  int result = minix_fs_read_file(path, data, size);

  if (result == 0)
  {
    /* File read successfully through VFS */
    serial_print("VFS: File read successfully: ");
    serial_print(path);
    serial_print("\n");
  }

  return result;
}

/**
 * Create user process - for ELF loader
 * This creates a new process structure for user programs
 */
int process_create_user(const char *name, uint64_t entry_point)
{
  if (!name)
  {
    return -1;
  }

  serial_print("VFS: Creating real user process for ");
  serial_print(name);
  serial_print("\n");

  /* Allocate memory for new process */

  process_t *new_process = (process_t *)kmalloc(sizeof(process_t));
  if (!new_process)
  {
    serial_print("VFS: Failed to allocate process structure\n");
    return -1;
  }

  /* Initialize process structure */
  memset(new_process, 0, sizeof(process_t));

  /* Set up basic process info */
  static pid_t next_user_pid = 100;
  new_process->task.pid = next_user_pid++;
  new_process->ppid = 1; /* Init process as parent */
  new_process->state = PROCESS_READY;
  new_process->task.state = TASK_READY;
  new_process->task.priority = 128; /* Default priority */
  new_process->task.nice = 0;       /* Default nice value */

  /* Set up user mode segments */
  new_process->task.cs = 0x1B; /* User code segment (GDT entry 3, RPL=3) */
  new_process->task.ss = 0x23; /* User data segment (GDT entry 4, RPL=3) */
  new_process->task.ds = 0x23;
  new_process->task.es = 0x23;
  new_process->task.fs = 0x23;
  new_process->task.gs = 0x23;

  /* Set up entry point */
  new_process->task.rip = entry_point;
  new_process->task.rflags = 0x202; /* Interrupts enabled, IOPL=0 */

/* Allocate user stack (4MB at high address) */
#define USER_STACK_SIZE (4 * 1024 * 1024) /* 4MB */
#define USER_STACK_BASE 0x7FFFF000        /* High user address */

  new_process->stack_start = USER_STACK_BASE;
  new_process->stack_size = USER_STACK_SIZE;
  new_process->task.rsp = USER_STACK_BASE; // Stack grows down
  new_process->task.rbp = USER_STACK_BASE;

  /* Set up heap (starts at 32MB) */
  new_process->heap_start = 0x2000000; // 32MB
  new_process->heap_end = 0x2000000;   // Initially empty

  /* Create page directory for user process */
  new_process->page_directory = (uint64_t *)create_process_page_directory();

  if (!new_process->page_directory)
  {
    serial_print("VFS: Failed to create page directory\n");
    kfree(new_process);
    return -1;
  }

  new_process->task.cr3 = (uint64_t)new_process->page_directory;

  /* Add to global process list */
  new_process->next = process_list;
  process_list = new_process;

  /* Add to scheduler */
  rr_add_process(new_process);
  serial_print("VFS: Process added to scheduler\n");

  serial_print("VFS: Created user process PID=");
  serial_print_hex32(new_process->task.pid);
  serial_print(" entry=");
  serial_print_hex32((uint32_t)entry_point);
  serial_print("\n");

  return (int)new_process->task.pid;
}