/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fs/minix_dir.c
 * Description: MINIX directory operations: mkdir/rmdir/unlink/link/ls/readdir.
 */

#include "minix_fs_internal.h"

int minix_fs_mkdir(const char *path, mode_t mode)
{
  if (!minix_fs.initialized)
  {
    return -ENODEV;
  }

  if (!path || kstrlen(path) == 0)
  {
    return -EINVAL;
  }

  if (kstrcmp(path, "/") == 0)
  {
    return -EEXIST;
  }

  if (!minix_root_block_present())
  {
    return -EIO;
  }

  char parent_path[VFS_PATH_MAX];
  char dirname[64];

  int rc_split = minix_fs_split_path(path, parent_path, dirname);
  if (rc_split != 0)
  {
    return rc_split;
  }

  {
    static const char *blocked[] = {
      "pid.new", "pid", "lock", "stat.new", "status.new", NULL
    };
    int bi;

    for (bi = 0; blocked[bi]; bi++)
    {
      if (kstrcmp(dirname, blocked[bi]) == 0)
        return -EEXIST;
    }
  }

  minix_inode_t parent_inode;
  minix_inode_t *parent_inode_ptr = minix_fs_find_inode(parent_path);
  if (!parent_inode_ptr)
  {
    return -ENOENT;
  }

  kmemcpy(&parent_inode, parent_inode_ptr, sizeof(minix_inode_t));

  if (!(parent_inode.i_mode & MINIX_IFDIR))
  {
    return -ENOTDIR;
  }

  uint16_t existing_inode = minix_fs_find_dir_entry(&parent_inode, dirname);
  if (existing_inode != 0)
  {
    minix_inode_t existing;

    if (minix_read_inode(existing_inode, &existing) == 0 &&
        (existing.i_mode & MINIX_IFMT) == 0)
    {
      if (minix_fs_remove_dir_entry(&parent_inode, dirname) != 0)
        return -EIO;
      minix_fs_free_inode(existing_inode);
    }
    else
    {
      return -EEXIST;
    }
  }

  uint16_t new_inode_num = minix_alloc_inode();
  if (new_inode_num == 0)
  {
    return -ENOSPC;
  }
  uint16_t parent_inode_num = minix_fs_get_inode_number(parent_path);
  if (parent_inode_num == 0)
  {
    minix_fs_free_inode(new_inode_num);
    return -EIO;
  }

  minix_inode_t new_inode;
  kmemset(&new_inode, 0, sizeof(minix_inode_t));

  // Apply umask to mode
  {
    const struct ir0_task_cred *cr = ir0_current_cred();

    mode_t effective_mode = mode & ~(cr ? cr->umask : (mode_t)0);
    /* POSIX: a new directory is owned by the creator's effective IDs. */
    new_inode.i_mode = MINIX_IFDIR | (effective_mode & 0777);
    new_inode.i_uid = cr ? (uint16_t)cr->euid : (uint16_t)0;
    new_inode.i_gid = cr ? (uint8_t)cr->egid : (uint8_t)0;
  }
  new_inode.i_size = MINIX_BLOCK_SIZE; // One block for . and ..
  new_inode.i_time = minix_now_seconds();
  new_inode.i_nlinks = 2; // . and ..

  // Allocate and initialize directory block
  uint32_t zone = minix_alloc_zone();
  if (zone == 0)
  {
    minix_fs_free_inode(new_inode_num);
    return -ENOSPC;
  }
  new_inode.i_zone[0] = zone;

  // Initialize directory block with . and ..
  uint8_t block_buffer[MINIX_BLOCK_SIZE] = {0};
  minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;

  // Add . entry (self)
  entries[0].inode = new_inode_num;
  kstrncpy(entries[0].name, ".", MINIX_NAME_LEN);

  // Add .. entry (parent)
  entries[1].inode = parent_inode_num;
  kstrncpy(entries[1].name, "..", MINIX_NAME_LEN);

  // Clear remaining entries
  size_t max_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);
  for (size_t i = 2; i < max_entries; i++)
  {
    entries[i].inode = 0;
    kmemset(entries[i].name, 0, MINIX_NAME_LEN);
  }

  if (minix_write_block(zone, block_buffer) != 0)
  {
    minix_free_zone(zone);
    minix_fs_free_inode(new_inode_num);
    return -EIO;
  }

  if (minix_fs_write_inode(new_inode_num, &new_inode) != 0)
  {
    minix_free_zone(zone);
    minix_fs_free_inode(new_inode_num);
    return -EIO;
  }

  if (minix_fs_add_dir_entry(&parent_inode, dirname, new_inode_num) != 0)
  {
    minix_free_zone(zone);
    minix_fs_free_inode(new_inode_num);
    return -EIO;
  }

  parent_inode.i_nlinks++;
  parent_inode.i_time = minix_now_seconds();

  if (minix_fs_write_inode(parent_inode_num, &parent_inode) != 0)
  {
    return -EIO;
  }

  if (minix_sync_zone_bitmap() != 0)
    return -EIO;

  return 0;
}

static void uint32_to_str(uint32_t num, char *buf, size_t buf_size)
{
  if (buf_size == 0)
    return;

  if (num == 0)
  {
    buf[0] = '0';
    buf[1] = '\0';
    return;
  }

  char tmp[16];
  int pos = 0;
  while (num > 0 && pos < 15)
  {
    tmp[pos++] = '0' + (num % 10);
    num /= 10;
  }

  int i = 0;
  for (int j = pos - 1; j >= 0 && i < (int)buf_size - 1; j--, i++)
  {
    buf[i] = tmp[j];
  }
  buf[i] = '\0';
}

int minix_fs_ls(const char *path, bool detailed)
{
  klog_print("SERIAL: minix_fs_ls called for path: ");
  klog_print(path ? path : "NULL");
  klog_print("\n");

  if (!minix_fs.initialized)
  {
    typewriter_vga_print("ls: filesystem not initialized\n", 0x0C);
    return -1;
  }

  if (!path)
  {
    typewriter_vga_print("ls: invalid path\n", 0x0C);
    return -1;
  }

  minix_inode_t *dir_inode = minix_fs_find_inode(path);
  if (!dir_inode)
  {
    char error_msg[VFS_PATH_MAX];
    snprintf(error_msg, sizeof(error_msg), "ls: cannot access '%s': No such file or directory\n", path);
    typewriter_vga_print(error_msg, 0x0C);
    return -ENOENT;
  }

  if (!(dir_inode->i_mode & MINIX_IFDIR))
  {
    char error_msg[VFS_PATH_MAX];
    snprintf(error_msg, sizeof(error_msg), "ls: cannot access '%s': Not a directory\n", path);
    typewriter_vga_print(error_msg, 0x0C);
    return -1;
  }

  minix_inode_t dir_inode_copy;
  kmemcpy(&dir_inode_copy, dir_inode, sizeof(minix_inode_t));

  if (detailed)
  {
    typewriter_vga_print("Permissions Links Size Owner Group Name\n", 0x0F);
  }

  int total_entries_printed = 0;

  for (int i = 0; i < MINIX_DIRECT_ZONES; i++)
  {
    uint32_t zone = dir_inode_copy.i_zone[i];
    if (zone == 0)
      continue;

    uint8_t block_buffer[MINIX_BLOCK_SIZE];
    if (minix_read_block(zone, block_buffer) != 0)
    {
      typewriter_vga_print("ls: error reading directory block\n", 0x0C);
      continue;
    }

    minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;
    int num_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);

    for (int j = 0; j < num_entries; j++)
    {
      if (entries[j].inode == 0)
        continue;

      // Safe name copy to ensure null termination
      char safe_name[MINIX_NAME_LEN + 1];
      // Copy manually to handle non-null terminated source
      int k;
      for (k = 0; k < MINIX_NAME_LEN; k++) {
          safe_name[k] = entries[j].name[k];
          if (safe_name[k] == '\0') break;
      }
      safe_name[k] = '\0';

      if (kstrcmp(safe_name, ".") == 0)
        continue;
      if (kstrcmp(safe_name, "..") == 0)
        continue;

      if (safe_name[0] == '\0')
        continue;

      if (detailed)
      {
        minix_inode_t entry_inode;
        if (minix_read_inode(entries[j].inode, &entry_inode) != 0)
        {
          typewriter_vga_print("ls: error reading inode\n", 0x0C);
          continue;
        }

        char perm_str[12];
        perm_str[0] = (entry_inode.i_mode & MINIX_IFDIR) ? 'd' : '-';
        perm_str[1] = (entry_inode.i_mode & MINIX_IRUSR) ? 'r' : '-';
        perm_str[2] = (entry_inode.i_mode & MINIX_IWUSR) ? 'w' : '-';
        perm_str[3] = (entry_inode.i_mode & MINIX_IXUSR) ? 'x' : '-';
        perm_str[4] = (entry_inode.i_mode & MINIX_IRGRP) ? 'r' : '-';
        perm_str[5] = (entry_inode.i_mode & MINIX_IWGRP) ? 'w' : '-';
        perm_str[6] = (entry_inode.i_mode & MINIX_IXGRP) ? 'x' : '-';
        perm_str[7] = (entry_inode.i_mode & MINIX_IROTH) ? 'r' : '-';
        perm_str[8] = (entry_inode.i_mode & MINIX_IWOTH) ? 'w' : '-';
        perm_str[9] = (entry_inode.i_mode & MINIX_IXOTH) ? 'x' : '-';
        perm_str[10] = ' ';
        perm_str[11] = '\0';

        char nlinks_str[16];
        uint32_to_str(entry_inode.i_nlinks, nlinks_str, sizeof(nlinks_str));

        char size_str[16];
        uint32_to_str(entry_inode.i_size, size_str, sizeof(size_str));

        char line[512];
        snprintf(line, sizeof(line), "%s %s %s root root %s\n",
                perm_str, nlinks_str, size_str, safe_name);
        typewriter_vga_print(line, 0x0F);
      }
      else
      {
        char line[VFS_PATH_MAX];
        int len = snprintf(line, sizeof(line), "%s\n", safe_name);

        klog_print("SERIAL: minix_fs_ls: printing line: '");
        klog_print(line);
        klog_print("' len=");
        klog_hex32(len);
        klog_print("\n");

        typewriter_vga_print(line, 0x0F);
        total_entries_printed++;
      }
    }
  }

  return 0;
}

int minix_fs_rm(const char *path)
{
  if (!minix_fs.initialized)
  {
    typewriter_vga_print("Error: MINIX filesystem not initialized\n", 0x0C);
    return -ENODEV;
  }

  if (!path || kstrlen(path) == 0)
  {
    typewriter_vga_print("Error: No file path specified\n", 0x0C);
    return -EINVAL;
  }

  if (kstrcmp(path, "/") == 0)
  {
    typewriter_vga_print("Error: Cannot remove root directory\n", 0x0C);
    return -EPERM;
  }

  /*
   * Snapshot child + parent: find_inode() returns a shared static buffer.
   * vfs_rename is link()+unlink(); unlink must decrement i_nlinks and only
   * free the inode at 0. Freeing while another name still points at the
   * inode (the rename destination) lets the next creat() reuse it — firstboot
   * then left /etc/shadow,/etc/group,/etc/fb.done as hardlinks to "ok\n".
   */
  uint16_t file_inode_num = minix_fs_get_inode_number(path);
  if (file_inode_num == 0)
  {
    char error_msg[VFS_PATH_MAX];
    snprintf(error_msg, sizeof(error_msg), "rm: '%s': No such file\n", path);
    typewriter_vga_print(error_msg, 0x0C);
    return -ENOENT;
  }

  minix_inode_t file_inode;
  if (minix_read_inode(file_inode_num, &file_inode) != 0)
    return -EIO;

  if (file_inode.i_mode & MINIX_IFDIR)
    return -EISDIR;

  char parent_path[VFS_PATH_MAX];
  char filename[64];

  int rc_split = minix_fs_split_path(path, parent_path, filename);
  if (rc_split != 0)
  {
    typewriter_vga_print("Error: Invalid path\n", 0x0C);
    return rc_split;
  }

  minix_inode_t *parent_inode_ptr = minix_fs_find_inode(parent_path);
  if (!parent_inode_ptr)
  {
    typewriter_vga_print("Error: Parent directory not found\n", 0x0C);
    return -ENOENT;
  }

  minix_inode_t parent_inode;
  kmemcpy(&parent_inode, parent_inode_ptr, sizeof(minix_inode_t));

  if (minix_fs_remove_dir_entry(&parent_inode, filename) != 0)
  {
    typewriter_vga_print("Error: Could not remove directory entry\n", 0x0C);
    return -EIO;
  }

  if (file_inode.i_nlinks > 0)
    file_inode.i_nlinks--;

  if (file_inode.i_nlinks == 0)
  {
    minix_inode_truncate_zones(&file_inode, file_inode_num,
			       file_inode.i_size, 0);
    file_inode.i_size = 0;
    if (minix_fs_write_inode(file_inode_num, &file_inode) != 0)
      return -EIO;
    if (minix_fs_free_inode(file_inode_num) != 0)
    {
      typewriter_vga_print("Error: Could not free inode\n", 0x0C);
      return -EIO;
    }
  }
  else if (minix_fs_write_inode(file_inode_num, &file_inode) != 0)
  {
    return -EIO;
  }

  uint16_t parent_inode_num = minix_fs_get_inode_number(parent_path);
  if (parent_inode_num != 0)
  {
    if (minix_fs_write_inode(parent_inode_num, &parent_inode) != 0)
    {
      typewriter_vga_print("Warning: Could not update parent directory\n", 0x0C);
      return -EIO;
    }
  }

  return 0;
}

int minix_fs_link(const char *oldpath, const char *newpath)
{
  if (!minix_fs.initialized)
  {
    typewriter_vga_print("Error: MINIX filesystem not initialized\n", 0x0C);
    return -ENODEV;
  }

  if (!oldpath || !newpath || kstrlen(oldpath) == 0 || kstrlen(newpath) == 0)
  {
    typewriter_vga_print("Error: Invalid path specified\n", 0x0C);
    return -EINVAL;
  }

  /*
   * Snapshot the old inode immediately: minix_fs_find_inode() returns a
   * pointer into a shared static buffer that every later lookup clobbers.
   * Holding the raw pointer across find_inode(newpath)/find_inode(parent)
   * would alias the parent directory inode onto old_inode and write its
   * IFDIR contents back to old_inode_num — corrupting the renamed file into
   * a directory (runsv "supervise/pid: is a directory").
   */
  minix_inode_t *old_inode_ptr = minix_fs_find_inode(oldpath);
  if (!old_inode_ptr)
  {
    char error_msg[VFS_PATH_MAX];
    snprintf(error_msg, sizeof(error_msg), "ln: '%s': No such file\n", oldpath);
    typewriter_vga_print(error_msg, 0x0C);
    return -ENOENT;
  }

  minix_inode_t old_inode;
  kmemcpy(&old_inode, old_inode_ptr, sizeof(minix_inode_t));

  // Cannot create hard link to a directory
  if (old_inode.i_mode & MINIX_IFDIR)
  {
    typewriter_vga_print("ln: cannot create hard link to directory\n", 0x0C);
    return -EPERM;
  }

  // Check if new path already exists
  if (minix_fs_find_inode(newpath) != NULL)
  {
    char error_msg[VFS_PATH_MAX];
    snprintf(error_msg, sizeof(error_msg), "ln: '%s': File exists\n", newpath);
    typewriter_vga_print(error_msg, 0x0C);
    return -EEXIST;
  }

  // Get the inode number of the old file
  uint16_t old_inode_num = minix_fs_get_inode_number(oldpath);
  if (old_inode_num == 0)
  {
    typewriter_vga_print("Error: Could not get inode number\n", 0x0C);
    return -EIO;
  }

  // Split new path into parent directory and filename
  char parent_path[VFS_PATH_MAX];
  char filename[64];
  int rc_split = minix_fs_split_path(newpath, parent_path, filename);
  if (rc_split != 0)
  {
    typewriter_vga_print("Error: Invalid new path\n", 0x0C);
    return rc_split;
  }

  // Get parent directory inode (snapshot before any further lookups)
  minix_inode_t *parent_inode_ptr = minix_fs_find_inode(parent_path);
  if (!parent_inode_ptr)
  {
    char error_msg[VFS_PATH_MAX];
    snprintf(error_msg, sizeof(error_msg), "ln: '%s': No such directory\n", parent_path);
    typewriter_vga_print(error_msg, 0x0C);
    return -ENOENT;
  }

  minix_inode_t parent_inode;
  kmemcpy(&parent_inode, parent_inode_ptr, sizeof(minix_inode_t));

  // Verify parent is a directory
  if (!(parent_inode.i_mode & MINIX_IFDIR))
  {
    typewriter_vga_print("ln: parent is not a directory\n", 0x0C);
    return -ENOTDIR;
  }

  // Add new directory entry pointing to the old inode
  if (minix_fs_add_dir_entry(&parent_inode, filename, old_inode_num) != 0)
  {
    typewriter_vga_print("Error: Could not add directory entry\n", 0x0C);
    return -EIO;
  }

  // Increment link count on the (snapshotted) inode and persist it
  old_inode.i_nlinks++;
  if (minix_fs_write_inode(old_inode_num, &old_inode) != 0)
  {
    typewriter_vga_print("Error: Could not update inode\n", 0x0C);
    return -EIO;
  }

  // Write updated parent directory
  uint16_t parent_inode_num = minix_fs_get_inode_number(parent_path);
  if (parent_inode_num != 0)
  {
    if (minix_fs_write_inode(parent_inode_num, &parent_inode) != 0)
    {
      typewriter_vga_print("Warning: Could not update parent directory\n", 0x0E);
    }
  }

  return 0;
}

/**
 * Remove a directory
 * @param path Path to the directory to remove
 * @return 0 on success, -1 on error
 */
int minix_fs_rmdir(const char *path)
{
  if (!minix_fs.initialized)
  {
    typewriter_vga_print("Error: MINIX filesystem not initialized\n", 0x0C);
    return -ENODEV;
  }

  if (!path || *path == '\0')
  {
    typewriter_vga_print("Error: No directory path specified\n", 0x0C);
    return -EINVAL;
  }

  if (kstrcmp(path, "/") == 0)
  {
    typewriter_vga_print("Error: Cannot remove root directory\n", 0x0C);
    return -EPERM;
  }

  /* Get inode number first */
  uint16_t dir_inode_num = minix_fs_get_inode_number(path);
  if (dir_inode_num == 0)
    return -ENOENT;

  /* Read inode directly from disk to ensure we have latest version */
  minix_inode_t dir_inode;
  if (minix_read_inode(dir_inode_num, &dir_inode) != 0)
    return -EIO;

  if (!(dir_inode.i_mode & MINIX_IFDIR))
    return -ENOTDIR;

  // Check if directory is empty (only . and .. allowed)
  bool is_empty = true;

  for (int i = 0; i < MINIX_DIRECT_ZONES && is_empty; i++)
  {
    uint32_t zone = dir_inode.i_zone[i];
    if (zone == 0)
    {
      continue; // No more zones
    }

    uint8_t block_buffer[MINIX_BLOCK_SIZE] = {0};
    if (minix_read_block(zone, block_buffer) != 0)
    {
      typewriter_vga_print("Error: Could not read directory block\n", 0x0C);
      return -EIO;
    }

    minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;
    int num_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);

    for (int j = 0; j < num_entries; j++)
    {
      // Skip empty entries
      if (entries[j].inode == 0)
      {
        continue;
      }

      // Only allow . and ..
      if (kstrcmp(entries[j].name, ".") != 0 &&
          kstrcmp(entries[j].name, "..") != 0)
      {
        is_empty = false;
        break;
      }
    }
  }

  if (!is_empty)
    return -ENOTEMPTY;

  char parent_path[VFS_PATH_MAX] = {0};
  char dirname[64] = {0};

  int rc_split = minix_fs_split_path(path, parent_path, dirname);
  if (rc_split != 0)
    return rc_split;

  minix_inode_t *parent_inode_ptr = minix_fs_find_inode(parent_path);
  if (!parent_inode_ptr)
    return -ENOENT;

  minix_inode_t parent_inode;
  kmemcpy(&parent_inode, parent_inode_ptr, sizeof(minix_inode_t));

  if (minix_fs_remove_dir_entry(&parent_inode, dirname) != 0)
    return -EIO;

  /* Free all zones used by the directory */
  for (int i = 0; i < MINIX_DIRECT_ZONES; i++)
  {
    if (dir_inode.i_zone[i] != 0)
    {
      minix_free_zone(dir_inode.i_zone[i]);
      dir_inode.i_zone[i] = 0;
    }
  }

  if (minix_fs_free_inode(dir_inode_num) != 0)
    return -EIO;

  uint16_t parent_inode_num = minix_fs_get_inode_number(parent_path);
  if (parent_inode_num != 0)
  {
    if (parent_inode.i_nlinks > 2)
      parent_inode.i_nlinks--;
    parent_inode.i_time = minix_now_seconds();
    if (minix_fs_write_inode(parent_inode_num, &parent_inode) != 0)
      return -EIO;
  }

  return 0;
}

/**
 * Force remove a directory without checking if it's empty
 * Use with caution - only for empty directories that can't be removed normally
 * @param path Path to the directory to remove
 * @return 0 on success, -1 on error
 */
int minix_fs_rmdir_force(const char *path)
{
  if (!minix_fs.initialized)
  {
    typewriter_vga_print("Error: MINIX filesystem not initialized\n", 0x0C);
    return -1;
  }

  if (!path || *path == '\0')
  {
    typewriter_vga_print("Error: No directory path specified\n", 0x0C);
    return -1;
  }

  if (kstrcmp(path, "/") == 0)
  {
    typewriter_vga_print("Error: Cannot remove root directory\n", 0x0C);
    return -1;
  }

  // Get inode number first
  uint16_t dir_inode_num = minix_fs_get_inode_number(path);
  if (dir_inode_num == 0)
  {
    char error_msg[VFS_PATH_MAX];
    snprintf(error_msg, sizeof(error_msg), "rmdir: '%s': No such file or directory\n", path);
    typewriter_vga_print(error_msg, 0x0C);
    return -1;
  }

  // Read inode directly from disk
  minix_inode_t dir_inode;
  if (minix_read_inode(dir_inode_num, &dir_inode) != 0)
  {
    char error_msg[VFS_PATH_MAX];
    snprintf(error_msg, sizeof(error_msg), "rmdir: '%s': Could not read inode\n", path);
    typewriter_vga_print(error_msg, 0x0C);
    return -1;
  }

  if (!(dir_inode.i_mode & MINIX_IFDIR))
  {
    char error_msg[VFS_PATH_MAX];
    snprintf(error_msg, sizeof(error_msg), "rmdir: '%s': Not a directory\n", path);
    typewriter_vga_print(error_msg, 0x0C);
    return -1;
  }

  // Skip empty check - force removal

  char parent_path[VFS_PATH_MAX] = {0};
  char dirname[64] = {0};

  int rc_split = minix_fs_split_path(path, parent_path, dirname);
  if (rc_split != 0)
  {
    typewriter_vga_print("Error: Invalid path\n", 0x0C);
    return rc_split;
  }

  minix_inode_t *parent_inode_ptr = minix_fs_find_inode(parent_path);
  if (!parent_inode_ptr)
  {
    typewriter_vga_print("Error: Parent directory not found\n", 0x0C);
    return -1;
  }

  minix_inode_t parent_inode;
  kmemcpy(&parent_inode, parent_inode_ptr, sizeof(minix_inode_t));

  if (minix_fs_remove_dir_entry(&parent_inode, dirname) != 0)
  {
    typewriter_vga_print("Error: Could not remove directory entry\n", 0x0C);
    return -1;
  }

  // Free all zones used by the directory
  for (int i = 0; i < MINIX_DIRECT_ZONES; i++)
  {
    if (dir_inode.i_zone[i] != 0)
    {
      minix_free_zone(dir_inode.i_zone[i]);
      dir_inode.i_zone[i] = 0;
    }
  }

  if (minix_fs_free_inode(dir_inode_num) != 0)
  {
    typewriter_vga_print("Error: Could not free inode\n", 0x0C);
    return -1;
  }

  uint16_t parent_inode_num = minix_fs_get_inode_number(parent_path);
  if (parent_inode_num != 0)
  {
    if (parent_inode.i_nlinks > 2)
    {
      parent_inode.i_nlinks--;
    }
    parent_inode.i_time = minix_now_seconds();

    if (minix_fs_write_inode(parent_inode_num, &parent_inode) != 0)
    {
      typewriter_vga_print("Warning: Could not update parent directory inode\n", 0x0C);
    }
  }

  return 0;
}

static int minix_backend_classified;

/*
 * minix_create - VFS create backend (path + mode only; no open flags).
 */
int minix_create(const char *path, mode_t mode)
{
  int ret;

  ret = minix_fs_touch(path, mode);
  if (ret == 0 && !minix_backend_classified)
  {
    minix_backend_classified = 1;
    if (DEBUG_VFS)
      klog_info("MINIX", "CLASSIFY MINIX_BACKEND_ONLY_CONFIRMED");
  }
  return ret;
}

/*
 * minix_fs_readdir - Read directory entries for VFS readdir
 */
int minix_fs_readdir(const char *path, struct vfs_dirent *entries, int max_entries)
{
	minix_inode_t *dir_inode = minix_fs_find_inode(path);
	if (!dir_inode)
		return -ENOENT;
	if (!minix_is_dir(dir_inode))
		return -ENOTDIR;

	int entry_count = 0;

  for (int i = 0; i < MINIX_DIRECT_ZONES && entry_count < max_entries; i++) {

    if (dir_inode->i_zone[i] == 0)
			continue;
		uint8_t block_buffer[MINIX_BLOCK_SIZE];

    if (minix_read_block(dir_inode->i_zone[i], block_buffer) != 0)
			continue;

    minix_dir_entry_t *minix_entries = (minix_dir_entry_t *)block_buffer;

    int num_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);

		for (int j = 0; j < num_entries && entry_count < max_entries; j++) {
			if (minix_entries[j].inode == 0)
				continue;

			if (minix_entries[j].name[0] == '\0')
				continue;

			if ((unsigned char)minix_entries[j].name[0] <= ' ')
				continue;

			minix_dirent_name(&minix_entries[j],
					  entries[entry_count].name);

			minix_inode_t target_inode;
			if (minix_read_inode(minix_entries[j].inode, &target_inode) != 0) {
				entries[entry_count].type = DT_UNKNOWN;
			} else if (minix_is_dir(&target_inode)) {
				entries[entry_count].type = DT_DIR;
			} else if (minix_is_reg(&target_inode)) {
				entries[entry_count].type = DT_REG;
			} else {
				entries[entry_count].type = DT_UNKNOWN;
			}
			entry_count++;
		}
	}
	return entry_count;
}
