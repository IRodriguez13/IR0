/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fs/minix_inode.c
 * Description: MINIX inode lookup/allocation and directory-entry primitives.
 */

#include "minix_fs_internal.h"

int minix_read_inode(uint32_t inode_num, minix_inode_t *inode)
{

  if (inode_num == 0 || inode_num >= MINIX_MAX_INODES || !inode)
  {
    klog_print("SERIAL: minix_read_inode: invalid parameters - inode_num=");
    klog_hex32(inode_num);
    klog_print("\n");
    return -1;
  }

  // Check if filesystem is initialized
  if (!minix_fs.initialized)
  {
    klog_print("SERIAL: minix_read_inode: filesystem not initialized!\n");
    return -1;
  }

  // Validate superblock values before using them
  if (minix_fs.superblock.s_magic != MINIX_SUPER_MAGIC)
  {
    log_debug_fmt("MINIX", "read_inode: invalid magic=0x%x", minix_fs.superblock.s_magic);
    return -1;
  }
  if (minix_fs.superblock.s_imap_blocks == 0 || minix_fs.superblock.s_zmap_blocks == 0)
  {
    log_debug_fmt("MINIX", "read_inode: invalid bitmaps imap=%u zmap=%u",
                  minix_fs.superblock.s_imap_blocks, minix_fs.superblock.s_zmap_blocks);
    return -1;
  }


  uint32_t inode_table_start =
      2 + minix_fs.superblock.s_imap_blocks + minix_fs.superblock.s_zmap_blocks;
  uint32_t inode_block =
      inode_table_start +
      ((inode_num - 1) * sizeof(minix_inode_t)) / MINIX_BLOCK_SIZE;
  uint32_t inode_offset =
      ((inode_num - 1) * sizeof(minix_inode_t)) % MINIX_BLOCK_SIZE;

  uint8_t block_buffer[MINIX_BLOCK_SIZE];
  int result = minix_read_block(inode_block, block_buffer);
  if (result != 0)
  {
    log_debug_fmt("MINIX", "read_inode: read_block failed ino=%u", inode_num);
    return -1;
  }

  kmemcpy(inode, block_buffer + inode_offset, sizeof(minix_inode_t));
  return 0;
}

static minix_inode_t cached_root_inode;
static minix_inode_t cached_result_inode;
bool root_inode_cached = false;
static uint16_t minix_last_resolved_ino;

/*
 * Path walk that copies the inode onto the caller. find_inode() used to
 * return a pointer to one of two static buffers; a preempted stat() then
 * filled struct stat from whatever the next walk left there (or from a
 * second get_inode_number() walk that raced with exec/ls).
 */
int minix_lookup(const char *pathname, minix_inode_t *out, uint16_t *ino_out)
{
  uint16_t current_inode_num = MINIX_ROOT_INODE;
  minix_inode_t current_inode;
  char path_copy[VFS_PATH_MAX];
  char *token;
  char *next;

  minix_last_resolved_ino = 0;

  if (!pathname || !out || !ino_out || !minix_fs.initialized)
    return -EINVAL;

  if (kstrcmp(pathname, "/") == 0)
  {
    int read_result = minix_read_inode(MINIX_ROOT_INODE, &cached_root_inode);

    if (read_result != 0)
    {
      log_debug_fmt("MINIX", "find_inode('/') read failed=%d", read_result);
      return -EIO;
    }
    root_inode_cached = true;
    kmemcpy(out, &cached_root_inode, sizeof(*out));
    *ino_out = MINIX_ROOT_INODE;
    minix_last_resolved_ino = MINIX_ROOT_INODE;
    return 0;
  }

  kstrncpy(path_copy, pathname, sizeof(path_copy) - 1);
  path_copy[sizeof(path_copy) - 1] = '\0';

  if (minix_read_inode(MINIX_ROOT_INODE, &current_inode) != 0)
    return -EIO;

  token = path_copy;
  if (*token == '/')
    token++;

  if (*token == '\0')
  {
    kmemcpy(out, &current_inode, sizeof(*out));
    *ino_out = current_inode_num;
    minix_last_resolved_ino = current_inode_num;
    return 0;
  }

  next = token;
  while (*next && *next != '/')
    next++;

  if (*next == '/')
    *next++ = '\0';
  else
    next = NULL;

  while (token != NULL && *token != '\0')
  {
    uint16_t found_inode;

    if (!(current_inode.i_mode & MINIX_IFDIR))
      return -ENOTDIR;

    found_inode = minix_fs_find_dir_entry(&current_inode, token);
    if (found_inode == 0)
      return -ENOENT;

    current_inode_num = found_inode;
    if (minix_read_inode(found_inode, &current_inode) != 0)
      return -EIO;

    if (next)
    {
      token = next;
      while (*next && *next != '/')
        next++;

      if (*next == '/')
        *next++ = '\0';
      else
        next = NULL;
    }
    else
    {
      token = NULL;
    }
  }

  kmemcpy(out, &current_inode, sizeof(*out));
  *ino_out = current_inode_num;
  minix_last_resolved_ino = current_inode_num;
  return 0;
}

minix_inode_t *minix_fs_find_inode(const char *pathname)
{
  if (minix_lookup(pathname, &cached_result_inode, &minix_last_resolved_ino) != 0)
    return NULL;
  return &cached_result_inode;
}

uint16_t minix_fs_get_inode_number(const char *pathname)
{
  minix_inode_t tmp;
  uint16_t ino;

  if (minix_lookup(pathname, &tmp, &ino) != 0)
    return 0;
  kmemcpy(&cached_result_inode, &tmp, sizeof(tmp));
  return ino;
}

uint16_t minix_fs_find_dir_entry(const minix_inode_t *dir_inode,
                                 const char *name)
{
  if (!dir_inode || !name || !(dir_inode->i_mode & MINIX_IFDIR))
  {
    return 0;
  }

  // Leer todas las zonas del directorio
  for (int i = 0; i < MINIX_DIRECT_ZONES; i++)
  {
    if (dir_inode->i_zone[i] == 0)
    {
      continue; // Zona vacía
    }

    uint8_t block_buffer[MINIX_BLOCK_SIZE];
    if (minix_read_block(dir_inode->i_zone[i], block_buffer) != 0)
    {
      continue;
    }

    // Buscar en las entradas del directorio
    minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;
    int num_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);

    for (int j = 0; j < num_entries; j++)
    {
      if (entries[j].inode == 0)
      {
        continue; // Entrada vacía
      }

      char ename[MINIX_NAME_LEN + 1];

      minix_dirent_name(&entries[j], ename);
      if (kstrcmp(ename, name) == 0)
      {
        return entries[j].inode;
      }
    }
  }

  return 0;
}

int minix_fs_write_inode(uint16_t inode_num, const minix_inode_t *inode)
{
  if (!inode || inode_num == 0)
  {
    return -EINVAL;
  }

  // Calcular la posición del inode en el disco (DEBE SER IGUAL A minix_read_inode)
  // Block layout: 0=boot, 1=super, 2=imap, 3=zmap, 4=inodes, 5+=data
  uint32_t inode_table_start =
      2 + minix_fs.superblock.s_imap_blocks + minix_fs.superblock.s_zmap_blocks;
  uint32_t inode_block =
      inode_table_start +
      ((inode_num - 1) * sizeof(minix_inode_t)) / MINIX_BLOCK_SIZE;
  uint32_t inode_offset =
      ((inode_num - 1) * sizeof(minix_inode_t)) % MINIX_BLOCK_SIZE;


#if DEBUG_VFS
  klog_print("SERIAL: write_inode: inode_num=");
  klog_hex32(inode_num);
  klog_print(" block=");
  klog_hex32(inode_block);
  klog_print(" offset=");
  klog_hex32(inode_offset);
  klog_print("\n");
#endif

  // Leer el bloque que contiene el inode
  uint8_t block_buffer[MINIX_BLOCK_SIZE];
  if (minix_read_block(inode_block, block_buffer) != 0)
  {
#if DEBUG_VFS
    klog_print("SERIAL: write_inode: failed to read block\n");
#endif
    return -EIO;
  }

  // Copiar el inode al buffer
  kmemcpy(block_buffer + inode_offset, inode, sizeof(minix_inode_t));

  // Escribir el bloque actualizado
  if (minix_write_block(inode_block, block_buffer) != 0)
  {
#if DEBUG_VFS
    klog_print("SERIAL: write_inode: failed to write block\n");
#endif
    return -EIO;
  }

  // Invalidate root cache if we wrote the root inode
  if (inode_num == MINIX_ROOT_INODE)
  {
    root_inode_cached = false;
  }

#if DEBUG_VFS
  klog_print("SERIAL: write_inode: success\n");
#endif
  return 0;
}

int minix_fs_free_inode(uint16_t inode_num)
{
  uint32_t byte_index;
  uint32_t bit_index;

  if (inode_num == 0 || inode_num > minix_fs.superblock.s_ninodes)
    return -EINVAL;

  /*
   * Inode bitmap lives at block 2 (same as minix_sync_inode_bitmap /
   * minix_alloc_inode). A previous off-by-one used block 1 and corrupted
   * s_zmap_blocks in the superblock — next mount then kmalloc'd ~64MiB.
   * Bit polarity matches alloc: bit set = used, clear = free.
   */
  byte_index = (uint32_t)inode_num / 8;
  bit_index = (uint32_t)inode_num % 8;
  if (byte_index >= (uint32_t)minix_fs.superblock.s_imap_blocks * MINIX_BLOCK_SIZE)
    return -EINVAL;

  if (minix_fs.inode_bitmap)
    minix_fs.inode_bitmap[byte_index] &= ~(1u << bit_index);

  if (minix_sync_inode_bitmap() != 0)
    return -EIO;
  return 0;
}

int minix_fs_split_path(const char *pathname, char *parent_path,
                        char *filename)
{
  if (!pathname || !parent_path || !filename)
  {
    return -EINVAL;
  }

  // Encontrar la última barra
  // Find last slash
  const char *last_slash = NULL;
  const char *p = pathname;
  while (*p)
  {
    if (*p == '/')
      last_slash = p;
    p++;
  }
  if (!last_slash)
  {
    size_t i;

    /* Relative path → treat as under root. Caller buffer ≥ MINIX_NAME_LEN+1. */
    kstrncpy(parent_path, "/", 2);
    for (i = 0; i < MINIX_NAME_LEN && pathname[i]; i++)
      filename[i] = pathname[i];
    filename[i] = '\0';
    if (pathname[i] != '\0')
      return -ENAMETOOLONG;
    return 0;
  }

  if (last_slash == pathname)
  {
    // Es el directorio raíz
    kstrncpy(parent_path, "/", 2);
  }
  else
  {
    // Copiar la parte del directorio padre
    size_t parent_len = last_slash - pathname;
    kstrncpy(parent_path, pathname, parent_len);
    parent_path[parent_len] = '\0';
  }

  /*
   * Up to 14 chars + NUL (classic MINIX name field width).
   *
   * A longer component used to be truncated here, so creating
   * "abcdefghijklmno" wrote an entry called "abcdefghijklmn" and the caller
   * then failed to find the name it asked for, reporting ENOENT while
   * leaving a differently named file on disk. Reject instead, the way the
   * Linux minix driver does.
   */
  {
    const char *base = last_slash + 1;
    size_t i;

    for (i = 0; i < MINIX_NAME_LEN && base[i]; i++)
      filename[i] = base[i];
    filename[i] = '\0';
    if (base[i] != '\0')
      return -ENAMETOOLONG;
  }

  return 0;
}

/**
 * Add a directory entry to the parent directory
 * @param parent_inode Parent directory inode (will be modified)
 * @param filename Name of the new entry
 * @param inode_num Inode number of the new entry
 * @return 0 on success, -1 on failure
 */
int minix_fs_add_dir_entry(minix_inode_t *parent_inode, const char *filename,
                           uint16_t inode_num)
{
  if (!parent_inode || !filename || inode_num == 0 || inode_num >= 65535)
  {
    return -EINVAL; // Invalid parameters
  }

  /*
   * Classic MINIX v1: name field is 14 bytes (may be unterminated if full).
   * Reject only names longer than MINIX_NAME_LEN (not >=).
   */
  size_t name_len = kstrlen(filename);
  if (name_len == 0 || name_len > MINIX_NAME_LEN)
  {
    return -EINVAL; // Invalid filename length
  }

  // Try to find a free entry in existing zones
  for (int zone_index = 0; zone_index < MINIX_DIRECT_ZONES; zone_index++)
  {
    if (parent_inode->i_zone[zone_index] == 0)
    {
      break; // No more allocated zones
    }

    uint32_t zone = parent_inode->i_zone[zone_index];
    uint8_t block_buffer[MINIX_BLOCK_SIZE] = {0};

    // Read the zone
    if (minix_read_block(zone, block_buffer) != 0)
    {
      continue; // Skip bad blocks
    }

    minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;
    int num_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);

    // Look for a free entry or check for duplicate filename
    for (int i = 0; i < num_entries; i++)
    {
      // Check for duplicate filename
      if (entries[i].inode != 0 && kstrncmp(entries[i].name, filename, MINIX_NAME_LEN) == 0)
      {
        return -EEXIST; // Entry with this name already exists
      }

      // Found a free entry
      if (entries[i].inode == 0)
      {
        // Initialize the entry
        entries[i].inode = inode_num;
        kmemcpy(entries[i].name, filename, name_len);
        if (name_len < MINIX_NAME_LEN)
          kmemset(entries[i].name + name_len, 0, MINIX_NAME_LEN - name_len);

        // Write the updated block back
        if (minix_write_block(zone, block_buffer) != 0)
        {
          return -EIO; // Failed to write block
        }

        // Update directory size if needed
        size_t entry_offset = (zone_index * MINIX_BLOCK_SIZE) + (i * sizeof(minix_dir_entry_t));
        if (entry_offset + sizeof(minix_dir_entry_t) > parent_inode->i_size)
        {
          parent_inode->i_size = entry_offset + sizeof(minix_dir_entry_t);
        }

        return 0; // Success
      }
    }
  }

  // No space in existing zones, allocate a new one
  for (int zone_index = 0; zone_index < MINIX_DIRECT_ZONES; zone_index++)
  {
    if (parent_inode->i_zone[zone_index] == 0)
    {
      // Allocate a new zone
      uint32_t new_zone = minix_alloc_zone();
      if (new_zone == 0)
      {
        return -ENOSPC; // No free zones
      }

      // Initialize the new zone with zeros
      uint8_t block_buffer[MINIX_BLOCK_SIZE] = {0};
      minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;

      // Add the new entry as the first one
      entries[0].inode = inode_num;
      kmemcpy(entries[0].name, filename, name_len);
      if (name_len < MINIX_NAME_LEN)
        kmemset(entries[0].name + name_len, 0, MINIX_NAME_LEN - name_len);

      // Write the new zone
      if (minix_write_block(new_zone, block_buffer) != 0)
      {
        minix_free_zone(new_zone); // Clean up
        return -EIO;
      }

      // Update parent inode
      parent_inode->i_zone[zone_index] = new_zone;
      parent_inode->i_size = (zone_index + 1) * MINIX_BLOCK_SIZE;

      return 0; // Success
    }
  }

  return -ENOSPC;
}

/**
 * Remove a directory entry from a directory
 * @param parent_inode Parent directory inode (will be modified)
 * @param filename Name of the entry to remove
 * @return 0 on success, -1 on failure
 */
int minix_fs_remove_dir_entry(minix_inode_t *parent_inode, const char *filename)
{
  if (!parent_inode || !filename || !*filename)
  {
    return -EINVAL; // Invalid parameters
  }

  // Search for the entry in all directory zones
  for (int zone_idx = 0; zone_idx < MINIX_DIRECT_ZONES; zone_idx++)
  {
    uint32_t zone = parent_inode->i_zone[zone_idx];
    if (zone == 0)
    {
      continue; // No more allocated zones
    }

    uint8_t block_buffer[MINIX_BLOCK_SIZE] = {0};
    if (minix_read_block(zone, block_buffer) != 0)
    {
      continue; // Skip bad blocks
    }

    minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;
    int num_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);
    bool entry_found = false;

    // First pass: find the entry
    for (int i = 0; i < num_entries; i++)
    {
      if (entries[i].inode == 0)
      {
        continue; // Skip empty entries
      }

      if (kstrncmp(entries[i].name, filename, MINIX_NAME_LEN) == 0)
      {
        // Found the entry, mark it as free
        entries[i].inode = 0;
        kmemset(entries[i].name, 0, MINIX_NAME_LEN);
        entry_found = true;
        break;
      }
    }

    if (entry_found)
    {
      // Write the updated block back
      if (minix_write_block(zone, block_buffer) != 0)
      {
        return -EIO; // Failed to write block
      }

      // Update directory size if needed
      size_t entry_size = sizeof(minix_dir_entry_t);
      size_t current_size = parent_inode->i_size;
      if (current_size > 0)
      {
        // Only shrink the directory if this was the last entry
        bool is_last_entry = true;
        for (int i = 0; i < num_entries; i++)
        {
          if (entries[i].inode != 0)
          {
            is_last_entry = false;
            break;
          }
        }

        if (is_last_entry)
        {
          // This was the last entry in this block
          if (parent_inode->i_size >= entry_size)
          {
            parent_inode->i_size -= entry_size;
          }
          else
          {
            parent_inode->i_size = 0;
          }
        }
      }

      return 0; // Success
    }
  }

  return -ENOENT;
}
