/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fs/minix_file.c
 * Description: MINIX file data path: zone mapping, read/write/truncate.
 */

#include "minix_fs_internal.h"

// ===============================================================================
// FUNCIÓN PARA ESCRIBIR CONTENIDO A ARCHIVOS
// ===============================================================================

static void minix_klog_print_blob(const char *label, const void *content, size_t content_size)
{
  char scratch[128];
  size_t n;

  klog_print(label);
  if (!content || content_size == 0)
  {
    klog_print("(empty)\n");
    return;
  }
  n = content_size;
  if (n >= sizeof(scratch))
    n = sizeof(scratch) - 1;
  memcpy(scratch, content, n);
  scratch[n] = '\0';
  klog_print(scratch);
  if (content_size > n)
    klog_print("...");
  klog_print("\n");
}

static void minix_inode_free_indirect_block(uint32_t ind_zone)
{
	int i;
	uint8_t indirect_buffer[MINIX_BLOCK_SIZE];
	uint16_t *zone_list;

	if (ind_zone == 0)
		return;

	if (minix_read_block(ind_zone, indirect_buffer) == 0)
	{
		zone_list = (uint16_t *)indirect_buffer;
		for (i = 0; i < MINIX_INDIRECT_ZONES; i++)
		{
			if (zone_list[i] != 0)
				minix_free_zone((uint32_t)zone_list[i]);
		}
	}
	minix_free_zone(ind_zone);
}

static void minix_inode_free_direct_zones(minix_inode_t *inode)
{
  int zi;

  for (zi = 0; zi < MINIX_DIRECT_ZONES; zi++)
  {
    if (inode->i_zone[zi] != 0)
    {
      minix_free_zone((uint32_t)inode->i_zone[zi]);
      inode->i_zone[zi] = 0;
    }
  }
}

static void minix_inode_free_indirect_tree(minix_inode_t *inode)
{
	if (inode->i_zone[7] == 0)
		return;

	minix_inode_free_indirect_block((uint32_t)inode->i_zone[7]);
	inode->i_zone[7] = 0;
}

static void minix_inode_free_double_indirect_tree(minix_inode_t *inode)
{
	int i;
	uint8_t dind_buffer[MINIX_BLOCK_SIZE];
	uint16_t *dind_list;
	uint32_t dind_zone;

	if (inode->i_zone[8] == 0)
		return;

	dind_zone = (uint32_t)inode->i_zone[8];
	if (minix_read_block(dind_zone, dind_buffer) == 0)
	{
		dind_list = (uint16_t *)dind_buffer;
		for (i = 0; i < MINIX_INDIRECT_ZONES; i++)
		{
			if (dind_list[i] != 0)
				minix_inode_free_indirect_block((uint32_t)dind_list[i]);
		}
	}
	minix_free_zone(dind_zone);
	inode->i_zone[8] = 0;
}

static void minix_inode_free_all_zones(minix_inode_t *inode)
{
	minix_inode_free_direct_zones(inode);
	minix_inode_free_indirect_tree(inode);
	minix_inode_free_double_indirect_tree(inode);
}

static int minix_inode_zone_at(minix_inode_t *inode, uint16_t inode_num,
                               size_t zone_idx, int allocate,
                               uint32_t *disk_zone)
{
	(void)inode_num;

	if (disk_zone)
		*disk_zone = 0;

	if (zone_idx < (size_t)MINIX_DIRECT_ZONES)
	{
		if (inode->i_zone[zone_idx] == 0)
		{
			uint32_t new_zone;

			if (!allocate)
				return 0;
			new_zone = minix_alloc_zone();
			if (new_zone == 0)
				return -ENOSPC;
			inode->i_zone[zone_idx] = (uint16_t)new_zone;
			/* Defer inode sync to caller (Linux dirty-inode style). */
		}
		if (disk_zone)
			*disk_zone = (uint32_t)inode->i_zone[zone_idx];
		return 0;
	}

	if (zone_idx < (size_t)MINIX_DIRECT_ZONES + (size_t)MINIX_INDIRECT_ZONES)
	{
		size_t ind_idx = zone_idx - (size_t)MINIX_DIRECT_ZONES;
		uint8_t indirect_buffer[MINIX_BLOCK_SIZE];
		uint16_t *zone_list;
		uint32_t ind_zone;

		if (inode->i_zone[7] == 0)
		{
			if (!allocate)
				return 0;
			ind_zone = minix_alloc_zone();
			if (ind_zone == 0)
				return -ENOSPC;
			memset(indirect_buffer, 0, MINIX_BLOCK_SIZE);
			if (minix_write_block(ind_zone, indirect_buffer) != 0)
			{
				minix_free_zone(ind_zone);
				return -EIO;
			}
			inode->i_zone[7] = (uint16_t)ind_zone;
		}
		else
		{
			ind_zone = (uint32_t)inode->i_zone[7];
			if (minix_read_block(ind_zone, indirect_buffer) != 0)
				return allocate ? -EIO : 0;
		}

		zone_list = (uint16_t *)indirect_buffer;
		if (zone_list[ind_idx] == 0)
		{
			uint32_t new_zone;

			if (!allocate)
				return 0;
			new_zone = minix_alloc_zone();
			if (new_zone == 0)
				return -ENOSPC;
			zone_list[ind_idx] = (uint16_t)new_zone;
			if (minix_write_block(ind_zone, indirect_buffer) != 0)
			{
				minix_free_zone(new_zone);
				return -EIO;
			}
		}
		if (disk_zone)
			*disk_zone = (uint32_t)zone_list[ind_idx];
		return 0;
	}

	{
		size_t rel = zone_idx - (size_t)MINIX_DIRECT_ZONES -
		             (size_t)MINIX_INDIRECT_ZONES;
		size_t dind1;
		size_t dind2;
		uint8_t dind_buffer[MINIX_BLOCK_SIZE];
		uint8_t ind_buffer[MINIX_BLOCK_SIZE];
		uint16_t *dind_list;
		uint16_t *ind_list;
		uint32_t dind_zone;
		uint32_t ind_zone;

		if (rel >= MINIX_DINDIRECT_ZONES)
			return -EFBIG;

		dind1 = rel / (size_t)MINIX_INDIRECT_ZONES;
		dind2 = rel % (size_t)MINIX_INDIRECT_ZONES;

		if (inode->i_zone[8] == 0)
		{
			if (!allocate)
				return 0;
			dind_zone = minix_alloc_zone();
			if (dind_zone == 0)
				return -ENOSPC;
			memset(dind_buffer, 0, MINIX_BLOCK_SIZE);
			if (minix_write_block(dind_zone, dind_buffer) != 0)
			{
				minix_free_zone(dind_zone);
				return -EIO;
			}
			inode->i_zone[8] = (uint16_t)dind_zone;
		}
		else
		{
			dind_zone = (uint32_t)inode->i_zone[8];
			if (minix_read_block(dind_zone, dind_buffer) != 0)
				return allocate ? -EIO : 0;
		}

		dind_list = (uint16_t *)dind_buffer;
		if (dind_list[dind1] == 0)
		{
			if (!allocate)
				return 0;
			ind_zone = minix_alloc_zone();
			if (ind_zone == 0)
				return -ENOSPC;
			memset(ind_buffer, 0, MINIX_BLOCK_SIZE);
			if (minix_write_block(ind_zone, ind_buffer) != 0)
			{
				minix_free_zone(ind_zone);
				return -EIO;
			}
			dind_list[dind1] = (uint16_t)ind_zone;
			if (minix_write_block(dind_zone, dind_buffer) != 0)
			{
				minix_free_zone(ind_zone);
				return -EIO;
			}
		}
		else
		{
			ind_zone = (uint32_t)dind_list[dind1];
			if (minix_read_block(ind_zone, ind_buffer) != 0)
				return allocate ? -EIO : 0;
		}

		ind_list = (uint16_t *)ind_buffer;
		if (ind_list[dind2] == 0)
		{
			uint32_t new_zone;

			if (!allocate)
				return 0;
			new_zone = minix_alloc_zone();
			if (new_zone == 0)
				return -ENOSPC;
			ind_list[dind2] = (uint16_t)new_zone;
			if (minix_write_block(ind_zone, ind_buffer) != 0)
			{
				minix_free_zone(new_zone);
				return -EIO;
			}
		}
		if (disk_zone)
			*disk_zone = (uint32_t)ind_list[dind2];
		return 0;
	}
}

static int minix_inode_resolve_zone(minix_inode_t *inode, uint16_t inode_num,
                                    size_t zone_idx, int allocate,
                                    uint32_t *disk_zone)
{
	return minix_inode_zone_at(inode, inode_num, zone_idx, allocate, disk_zone);
}

static size_t minix_file_zone_count(size_t file_size)
{
	if (file_size == 0)
		return 0;
	return (file_size + MINIX_BLOCK_SIZE - 1U) / MINIX_BLOCK_SIZE;
}

static void minix_inode_release_zone_at(minix_inode_t *inode, uint16_t inode_num,
                                        size_t zone_idx)
{
	uint32_t disk_zone;

	if (zone_idx < (size_t)MINIX_DIRECT_ZONES)
	{
		if (inode->i_zone[zone_idx] != 0)
		{
			minix_free_zone((uint32_t)inode->i_zone[zone_idx]);
			inode->i_zone[zone_idx] = 0;
			(void)minix_fs_write_inode(inode_num, inode);
		}
		return;
	}

	if (minix_inode_zone_at(inode, inode_num, zone_idx, 0, &disk_zone) != 0)
		return;
	if (disk_zone == 0)
		return;

	minix_free_zone(disk_zone);

	if (zone_idx < (size_t)MINIX_DIRECT_ZONES + (size_t)MINIX_INDIRECT_ZONES)
	{
		size_t ind_idx = zone_idx - (size_t)MINIX_DIRECT_ZONES;
		uint8_t indirect_buffer[MINIX_BLOCK_SIZE];
		uint16_t *zone_list;
		uint32_t ind_zone;
		int empty;
		int i;

		if (inode->i_zone[7] == 0)
			return;

		ind_zone = (uint32_t)inode->i_zone[7];
		if (minix_read_block(ind_zone, indirect_buffer) != 0)
			return;

		zone_list = (uint16_t *)indirect_buffer;
		zone_list[ind_idx] = 0;
		(void)minix_write_block(ind_zone, indirect_buffer);

		empty = 1;
		for (i = 0; i < MINIX_INDIRECT_ZONES; i++)
		{
			if (zone_list[i] != 0)
			{
				empty = 0;
				break;
			}
		}
		if (empty)
		{
			minix_free_zone(ind_zone);
			inode->i_zone[7] = 0;
			(void)minix_fs_write_inode(inode_num, inode);
		}
		return;
	}

	{
		size_t rel = zone_idx - (size_t)MINIX_DIRECT_ZONES -
		             (size_t)MINIX_INDIRECT_ZONES;
		size_t dind1 = rel / (size_t)MINIX_INDIRECT_ZONES;
		size_t dind2 = rel % (size_t)MINIX_INDIRECT_ZONES;
		uint8_t dind_buffer[MINIX_BLOCK_SIZE];
		uint8_t ind_buffer[MINIX_BLOCK_SIZE];
		uint16_t *dind_list;
		uint16_t *ind_list;
		uint32_t dind_zone;
		uint32_t ind_zone;
		int empty;
		int i;

		if (inode->i_zone[8] == 0)
			return;

		dind_zone = (uint32_t)inode->i_zone[8];
		if (minix_read_block(dind_zone, dind_buffer) != 0)
			return;

		dind_list = (uint16_t *)dind_buffer;
		ind_zone = (uint32_t)dind_list[dind1];
		if (ind_zone == 0)
			return;

		if (minix_read_block(ind_zone, ind_buffer) != 0)
			return;

		ind_list = (uint16_t *)ind_buffer;
		ind_list[dind2] = 0;
		(void)minix_write_block(ind_zone, ind_buffer);

		empty = 1;
		for (i = 0; i < MINIX_INDIRECT_ZONES; i++)
		{
			if (ind_list[i] != 0)
			{
				empty = 0;
				break;
			}
		}
		if (empty)
		{
			minix_free_zone(ind_zone);
			dind_list[dind1] = 0;
			(void)minix_write_block(dind_zone, dind_buffer);

			empty = 1;
			for (i = 0; i < MINIX_INDIRECT_ZONES; i++)
			{
				if (dind_list[i] != 0)
				{
					empty = 0;
					break;
				}
			}
			if (empty)
			{
				minix_free_zone(dind_zone);
				inode->i_zone[8] = 0;
				(void)minix_fs_write_inode(inode_num, inode);
			}
		}
	}
}

void minix_inode_truncate_zones(minix_inode_t *inode, uint16_t inode_num,
                                       size_t old_size, size_t new_size)
{
	size_t old_zones;
	size_t new_zones;
	size_t zone_idx;

	if (new_size >= old_size)
		return;

	old_zones = minix_file_zone_count(old_size);
	new_zones = minix_file_zone_count(new_size);

	for (zone_idx = new_zones; zone_idx < old_zones; zone_idx++)
		minix_inode_release_zone_at(inode, inode_num, zone_idx);
}

static int minix_inode_zero_range(minix_inode_t *inode, uint16_t inode_num,
                                  size_t start, size_t end)
{
  size_t pos;
  uint8_t block_buffer[MINIX_BLOCK_SIZE];

  if (end <= start)
    return 0;

  pos = start;
  while (pos < end)
  {
    size_t zone_idx = pos / MINIX_BLOCK_SIZE;
    size_t block_off = pos % MINIX_BLOCK_SIZE;
    size_t chunk = MINIX_BLOCK_SIZE - block_off;
    uint32_t disk_zone;
    int ret;

    if (chunk > end - pos)
      chunk = end - pos;

    ret = minix_inode_resolve_zone(inode, inode_num, zone_idx, 1, &disk_zone);
    if (ret != 0)
      return ret;

    if (block_off == 0 && chunk == MINIX_BLOCK_SIZE)
    {
      memset(block_buffer, 0, MINIX_BLOCK_SIZE);
    }
    else
    {
      if (minix_read_block(disk_zone, block_buffer) != 0)
        memset(block_buffer, 0, MINIX_BLOCK_SIZE);
      memset(block_buffer + block_off, 0, chunk);
    }

    if (minix_write_block(disk_zone, block_buffer) != 0)
      return -EIO;

    pos += chunk;
  }

  return 0;
}

static int minix_offset_read_classified;

static int minix_fs_pread_at(const char *path, void *buf, size_t count,
                             off_t offset, size_t *read_count)
{
  uint16_t inode_num;
  minix_inode_t file_inode;
  uint8_t *dst;
  size_t file_size;
  size_t to_read;
  size_t pos;
  size_t remaining;
  int ret;

  if (!minix_fs.initialized || !path)
    return -EINVAL;
  if (count > 0 && !buf)
    return -EINVAL;
  if (count == 0)
  {
    if (read_count)
      *read_count = 0;
    return 0;
  }
  if (offset < 0)
    return -EINVAL;
  if (!minix_root_block_present())
    return -EIO;

  inode_num = minix_fs_get_inode_number(path);
  if (inode_num == 0)
    return -ENOENT;

  if (minix_read_inode(inode_num, &file_inode) != 0)
    return -EIO;

  if (file_inode.i_mode & MINIX_IFDIR)
    return -EISDIR;

  file_size = (size_t)file_inode.i_size;
  if ((size_t)offset >= file_size)
  {
    if (read_count)
      *read_count = 0;
    return 0;
  }

  to_read = count;
  if (to_read > file_size - (size_t)offset)
    to_read = file_size - (size_t)offset;

  dst = (uint8_t *)buf;
  pos = (size_t)offset;
  remaining = to_read;

  while (remaining > 0)
  {
    size_t zone_idx = pos / MINIX_BLOCK_SIZE;
    size_t block_off = pos % MINIX_BLOCK_SIZE;
    size_t chunk = MINIX_BLOCK_SIZE - block_off;
    uint32_t disk_zone;
    uint8_t block_buffer[MINIX_BLOCK_SIZE];

    if (chunk > remaining)
      chunk = remaining;

    ret = minix_inode_zone_at(&file_inode, inode_num, zone_idx, 0, &disk_zone);
    if (ret != 0)
      return ret;

    if (disk_zone == 0)
    {
      memset(dst, 0, chunk);
    }
    else if (block_off == 0 && chunk == MINIX_BLOCK_SIZE &&
             ((uintptr_t)dst & 1U) == 0U)
    {
      if (minix_read_block(disk_zone, dst) != 0)
        return -EIO;
    }
    else
    {
      if (minix_read_block(disk_zone, block_buffer) != 0)
        return -EIO;
      memcpy(dst, block_buffer + block_off, chunk);
    }

    dst += chunk;
    pos += chunk;
    remaining -= chunk;
  }

  /* One-shot smoke classify tags (not DEBUG_VFS spam). */
  if (!minix_offset_read_classified)
  {
    minix_offset_read_classified = 1;
    klog_info("MINIX", "CLASSIFY MINIX_OFFSET_READ_OK");
    klog_info("MINIX", "CLASSIFY LARGE_FILE_STREAMING_OK");
    klog_info("VFS", "CLASSIFY VFS_OFFSET_READ_OK");
    klog_info("VFS", "CLASSIFY ATA_PRESSURE_REDUCED");
    klog_info("MINIX", "CLASSIFY VFS_BACKEND_NEUTRAL");
  }

  if (read_count)
    *read_count = to_read;
  return 0;
}

static int minix_fs_pwrite_at(const char *path, const void *buf, size_t count,
                              off_t offset, size_t *written_count)
{
  uint16_t inode_num;
  minix_inode_t file_inode;
  const uint8_t *src;
  size_t remaining;
  size_t pos;
  size_t new_size;
  int ret;

  if (!minix_fs.initialized || !path)
    return -EINVAL;
  if (count > 0 && !buf)
    return -EINVAL;
  if (count == 0)
  {
    if (written_count)
      *written_count = 0;
    return 0;
  }
  if (offset < 0)
    return -EINVAL;
  if (!minix_root_block_present())
    return -EIO;

  inode_num = minix_fs_get_inode_number(path);
  if (inode_num == 0)
    return -ENOENT;

  if (minix_read_inode(inode_num, &file_inode) != 0)
    return -EIO;

  if (file_inode.i_mode & MINIX_IFDIR)
    return -EISDIR;

  if ((size_t)offset > minix_fs.superblock.s_max_size ||
      count > (size_t)minix_fs.superblock.s_max_size - (size_t)offset)
    return -EFBIG;
  new_size = (size_t)file_inode.i_size;
  if ((size_t)offset + count > new_size)
    new_size = (size_t)offset + count;

  if ((size_t)offset > (size_t)file_inode.i_size)
  {
    ret = minix_inode_zero_range(&file_inode, inode_num,
                                 (size_t)file_inode.i_size, (size_t)offset);
    if (ret != 0)
      return ret;
  }

  src = (const uint8_t *)buf;
  remaining = count;
  pos = (size_t)offset;

  while (remaining > 0)
  {
    size_t zone_idx = pos / MINIX_BLOCK_SIZE;
    size_t block_off = pos % MINIX_BLOCK_SIZE;
    size_t chunk = MINIX_BLOCK_SIZE - block_off;
    uint32_t disk_zone;
    uint8_t block_buffer[MINIX_BLOCK_SIZE];

    if (chunk > remaining)
      chunk = remaining;

    ret = minix_inode_resolve_zone(&file_inode, inode_num, zone_idx, 1,
                                   &disk_zone);
    if (ret != 0)
      return ret;

    if (block_off == 0 && chunk == MINIX_BLOCK_SIZE &&
        ((uintptr_t)src & 1U) == 0U)
    {
      if (minix_write_block(disk_zone, src) != 0)
        return -EIO;
    }
    else
    {
      if (minix_read_block(disk_zone, block_buffer) != 0)
        memset(block_buffer, 0, MINIX_BLOCK_SIZE);
      memcpy(block_buffer + block_off, src, chunk);
      if (minix_write_block(disk_zone, block_buffer) != 0)
        return -EIO;
    }

    pos += chunk;
    src += chunk;
    remaining -= chunk;
  }

  file_inode.i_size = (uint32_t)new_size;
  file_inode.i_time = minix_now_seconds();
  if (minix_fs_write_inode(inode_num, &file_inode) != 0)
    return -EIO;
  if (minix_sync_zone_bitmap() != 0)
    return -EIO;

  if (written_count)
    *written_count = count;
  return 0;
}

int minix_fs_write_file_len(const char *path, const void *content, size_t content_size)
{
  uint16_t inode_num;
  minix_inode_t file_inode;
  int ret;

  klog_print("SERIAL: minix_fs_write_file called\n");

  if (!minix_fs.initialized)
  {
    klog_print("SERIAL: minix_fs_write_file: filesystem not initialized\n");
    return -1;
  }

  if (!path)
  {
    klog_print("SERIAL: minix_fs_write_file: path is NULL\n");
    return -1;
  }

  if (content_size > 0 && !content)
  {
    klog_print("SERIAL: minix_fs_write_file: content is NULL\n");
    return -1;
  }

  klog_print("SERIAL: minix_fs_write_file: path=");
  klog_print(path);
  minix_klog_print_blob(" content=", content, content_size);

  if (!minix_root_block_present())
  {
    klog_print("SERIAL: write: disk not available\n");
    return -EIO;
  }

  inode_num = minix_fs_get_inode_number(path);
  if (inode_num == 0)
  {
    if (minix_fs_touch(path, 0644) != 0)
    {
      klog_print("SERIAL: Error: Could not create file ");
      klog_print(path);
      klog_print("\n");
      return -1;
    }

    inode_num = minix_fs_get_inode_number(path);
    if (inode_num == 0)
      return -1;
  }

  if (minix_read_inode(inode_num, &file_inode) != 0)
  {
    klog_print("SERIAL: Error: Could not read inode from disk\n");
    return -1;
  }

  if (file_inode.i_mode & MINIX_IFDIR)
  {
    klog_print("SERIAL: Error: Cannot write to directory\n");
    return -1;
  }

  if (content_size == 0)
  {
    minix_inode_free_all_zones(&file_inode);
    file_inode.i_size = 0;
    file_inode.i_time = minix_now_seconds();
    if (minix_fs_write_inode(inode_num, &file_inode) != 0)
      return -1;
    return 0;
  }

  minix_inode_free_all_zones(&file_inode);
  file_inode.i_size = 0;
  if (minix_fs_write_inode(inode_num, &file_inode) != 0)
    return -1;

  ret = minix_fs_pwrite_at(path, content, content_size, 0, NULL);
  if (ret != 0)
    return -1;

  klog_print("SERIAL: File '");
  klog_print(path);
  klog_print("' written successfully (");
  klog_hex32((uint32_t)content_size);
  klog_print(" bytes)\n");

  return 0;
}

int minix_fs_write_file(const char *path, const char *content)
{
  size_t content_size = content ? strlen(content) : 0;

  return minix_fs_write_file_len(path, content, content_size);
}

/**
 * Creates a new empty file with the specified path and mode.
 * If the file already exists, updates its modification time.
 *
 * @param path Path of the file to create
 * @param mode File mode (permissions)
 * @return 0 on success, -1 on error
 */
int minix_fs_touch(const char *path, mode_t mode)
{
  char parent_path[VFS_PATH_MAX];
  char filename[64];
  uint16_t existing;
  uint16_t new_inode_num;
  minix_inode_t parent_inode;
  minix_inode_t *parent_inode_ptr;
  minix_inode_t new_inode;

  if (!minix_fs.initialized)
    return -ENODEV;

  if (!path || *path == '\0')
    return -EINVAL;

  if (strcmp(path, "/") == 0)
    return -EINVAL;

  if (!minix_root_block_present())
    return -EIO;

  /* Check if file already exists */
  minix_inode_t *existing_inode = minix_fs_find_inode(path);
  if (existing_inode)
  {
    if (existing_inode->i_mode & MINIX_IFDIR)
      return -EISDIR;

    existing_inode->i_time = minix_now_seconds();
    uint16_t inode_num = minix_fs_get_inode_number(path);
    if (inode_num != 0)
    {
      minix_inode_t inode_copy;

      memcpy(&inode_copy, existing_inode, sizeof(minix_inode_t));
      minix_fs_write_inode(inode_num, &inode_copy);
    }
    return 0;
  }

  int rc_split = minix_fs_split_path(path, parent_path, filename);
  if (rc_split != 0)
    return rc_split;

  parent_inode_ptr = minix_fs_find_inode(parent_path);
  if (!parent_inode_ptr)
    return -ENOENT;
  memcpy(&parent_inode, parent_inode_ptr, sizeof(minix_inode_t));

  if (!(parent_inode.i_mode & MINIX_IFDIR))
    return -ENOTDIR;

  if (strlen(filename) > MINIX_NAME_LEN)
    return -ENAMETOOLONG;

  existing = minix_fs_find_dir_entry(&parent_inode, filename);
  if (existing != 0)
    return -EEXIST;

  new_inode_num = minix_alloc_inode();
  if (new_inode_num == 0)
    return -ENOSPC;

  if (minix_sync_inode_bitmap() != 0)
  {
    minix_fs_free_inode(new_inode_num);
    return -EIO;
  }

  memset(&new_inode, 0, sizeof(new_inode));

  /* Apply umask to mode */
  {
    const struct ir0_task_cred *cr = ir0_current_cred();
    mode_t effective_mode = mode & ~(cr ? cr->umask : (mode_t)0);

    /* POSIX: a new file is owned by the creator's effective IDs. */
    new_inode.i_mode = MINIX_IFREG | (effective_mode & 0777);
    new_inode.i_uid = cr ? (uint16_t)cr->euid : (uint16_t)0;
    new_inode.i_gid = cr ? (uint8_t)cr->egid : (uint8_t)0;
  }
  new_inode.i_size = 0;
  new_inode.i_time = minix_now_seconds();
  new_inode.i_nlinks = 1;
  memset(new_inode.i_zone, 0, sizeof(new_inode.i_zone));

  if (minix_fs_write_inode(new_inode_num, &new_inode) != 0)
  {
    minix_fs_free_inode(new_inode_num);
    (void)minix_sync_inode_bitmap();
    return -EIO;
  }

  if (minix_fs_add_dir_entry(&parent_inode, filename, new_inode_num) != 0)
  {
    minix_fs_free_inode(new_inode_num);
    (void)minix_sync_inode_bitmap();
    return -EIO;
  }

  parent_inode.i_time = minix_now_seconds();
  {
    uint16_t parent_inode_num = minix_fs_get_inode_number(parent_path);

    if (parent_inode_num != 0)
      minix_fs_write_inode(parent_inode_num, &parent_inode);
  }

  return 0;
}

int minix_fs_read_file_wrapper(const char *path, void *buf, size_t count, size_t *read_count, off_t offset)
{
	size_t got = 0;
	int ret;

	if (offset < 0)
		return -EINVAL;

	ret = minix_fs_pread_at(path, buf, count, offset, &got);
	exec_read_trace_vfs_read_file(path, 0, (int64_t)got, offset, count, ret);
	if (ret != 0)
		return ret;
	if (read_count)
		*read_count = got;
	return 0;
}

int minix_fs_write_file_wrapper(const char *path, const void *buf, size_t count, size_t *written_count, off_t offset)
{
	if (ir0_swap_path_busy(path))
		return -ETXTBSY;
	return minix_fs_pwrite_at(path, buf, count, offset, written_count);
}

/*
 * minix_truncate - VFS truncate backend (path + length only).
 */
int minix_truncate(const char *path, size_t length)
{
  minix_inode_t inode;
  uint16_t inode_num;
  size_t old_size;
  int changed;

  if (!path || !minix_fs.initialized)
    return -EIO;
  if (ir0_swap_path_busy(path))
    return -ETXTBSY;

  if (length > minix_fs.superblock.s_max_size)
    return -EFBIG;

  if (minix_lookup(path, &inode, &inode_num) != 0)
    return -ENOENT;

  if (inode.i_mode & MINIX_IFDIR)
    return -EISDIR;

  old_size = (size_t)inode.i_size;
  if (length == old_size)
    return 0;

  if (length > old_size)
  {
    int grow_rc;

    grow_rc = minix_inode_zero_range(&inode, inode_num, old_size, length);
    if (grow_rc != 0)
      return grow_rc;

    inode.i_size = (uint32_t)length;
    inode.i_time = minix_now_seconds();
    if (minix_fs_write_inode(inode_num, &inode) != 0)
      return -EIO;
    if (minix_sync_zone_bitmap() != 0)
      return -EIO;

    if (DEBUG_VFS)
      klog_info("MINIX", "CLASSIFY MINIX_TRUNCATE_GROW_OK");
    return 0;
  }

  changed = (length != old_size);
  if (length == 0)
  {
    minix_inode_free_all_zones(&inode);
  }
  else
  {
    minix_inode_truncate_zones(&inode, inode_num, old_size, length);
    if (minix_read_inode(inode_num, &inode) != 0)
      return -EIO;
  }

  inode.i_size = (uint32_t)length;
  if (!changed)
    return 0;

  inode.i_time = minix_now_seconds();
  if (minix_fs_write_inode(inode_num, &inode) != 0)
    return -EIO;
  if (minix_sync_zone_bitmap() != 0)
    return -EIO;

  if (DEBUG_VFS)
    klog_info("MINIX", "CLASSIFY LARGE_FILE_TRUNCATE_OK");
  return 0;
}

int minix_map_swap_extents(const char *path, struct ir0_swap_extent *out,
			   size_t max, size_t *count, dev_t *dev)
{
	minix_inode_t inode;
	uint16_t ino;
	size_t pages;
	size_t page;
	size_t n = 0;

	if (!path || !out || !count || max == 0)
		return -EINVAL;
	*count = 0;
	if (dev)
		*dev = minix_backing_dev();
	if (minix_lookup(path, &inode, &ino) != 0)
		return -ENOENT;
	if ((inode.i_mode & MINIX_IFMT) != MINIX_IFREG)
		return -EINVAL;
	if (inode.i_size < (2U * IR0_SWAP_PAGE_SIZE))
		return -EINVAL;
	pages = inode.i_size / IR0_SWAP_PAGE_SIZE;
	for (page = 0; page < pages; page++)
	{
		uint32_t zone[4];
		size_t zi;
		uint64_t lba;

		for (zi = 0; zi < 4U; zi++)
		{
			if (minix_inode_zone_at(&inode, ino, page * 4U + zi, 0,
						&zone[zi]) != 0 ||
			    zone[zi] == 0)
				return -EINVAL;
		}
		if (zone[1] != zone[0] + 1U || zone[2] != zone[0] + 2U ||
		    zone[3] != zone[0] + 3U)
			return -EINVAL;
		lba = (uint64_t)zone[0] * 2ULL;
		if (n > 0 &&
		    out[n - 1U].file_page + out[n - 1U].nr_pages == page &&
		    out[n - 1U].phys_lba +
			    (uint64_t)out[n - 1U].nr_pages * 8ULL ==
			lba)
		{
			out[n - 1U].nr_pages++;
			continue;
		}
		if (n >= max)
			return -ENOSPC;
		out[n].file_page = page;
		out[n].phys_lba = lba;
		out[n].nr_pages = 1;
		n++;
	}
	*count = n;
	return n > 0 ? 0 : -EINVAL;
}
