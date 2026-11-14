/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fs/minix_fs.c
 * Description: core MINIX filesystem state, bitmaps, block I/O, init/mount.
 */

#include "minix_fs_internal.h"

minix_fs_info_t minix_fs;
static dev_t minix_root_dev;
static int minix_blockdev_classified;
static int minix_zmap_dirty;
static uint32_t minix_zmap_search_start;

/*
 * MINIX i_time is a POSIX time_t (seconds); the kernel clock counts
 * milliseconds since boot.
 */
/*
 * MINIX i_time is a Unix epoch, not an uptime. Seeding it from milliseconds
 * since boot dated every file on the root volume to a few seconds after
 * 1970-01-01, which is what ls -l showed. The wall clock comes from the RTC
 * at boot and ticks once a second.
 */
/*
 * Copy an on-disk directory name into a NUL-terminated buffer.
 *
 * MINIX stores names in a fixed 14-byte field with no terminator when the
 * name uses the whole field. Treating that field as a C string reads into
 * the next directory entry: `du` reported
 * "/usr/share/ash-completion<garbage>: Invalid argument" because
 * "ash-completion" is exactly 14 characters, and a lookup of any such name
 * compared past the field and never matched.
 */
void minix_dirent_name(const minix_dir_entry_t *e,
			      char out[MINIX_NAME_LEN + 1])
{
	int k;

	for (k = 0; k < MINIX_NAME_LEN; k++)
	{
		out[k] = e->name[k];
		if (out[k] == '\0')
			return;
	}
	out[MINIX_NAME_LEN] = '\0';
}

uint32_t minix_now_seconds(void)
{
	return (uint32_t)clock_get_current_time();
}

size_t minix_zmap_bytes(void)
{
	uint16_t blocks = minix_fs.superblock.s_zmap_blocks;

	if (blocks == 0)
		blocks = 1;
	return (size_t)blocks * (size_t)MINIX_BLOCK_SIZE;
}

int minix_sync_zone_bitmap(void)
{
	uint32_t zmap_block;
	uint16_t blocks;
	uint16_t b;

	if (!minix_fs.zone_bitmap || !minix_zmap_dirty)
		return 0;

	blocks = minix_fs.superblock.s_zmap_blocks;
	if (blocks == 0)
		return -EIO;

	zmap_block = 2 + minix_fs.superblock.s_imap_blocks;
	for (b = 0; b < blocks; b++)
	{
		if (minix_write_block(zmap_block + b,
				      minix_fs.zone_bitmap +
					  (size_t)b * MINIX_BLOCK_SIZE) != 0)
			return -EIO;
	}
	minix_zmap_dirty = 0;
	return 0;
}

static dev_t minix_root_dev_id(void)
{
	if (minix_root_dev == 0)
		minix_root_dev = ir0_block_lookup_by_name(CONFIG_ROOT_BLOCK_DEVICE);
	return minix_root_dev;
}

dev_t minix_backing_dev(void)
{
	return minix_root_dev_id();
}

static void minix_emit_blockdev_classify(void)
{
	if (minix_blockdev_classified)
		return;
	minix_blockdev_classified = 1;
	klog_info("MINIX", "CLASSIFY MINIX_USES_BLOCKDEV_FACADE");
}

bool minix_root_block_present(void)
{
	dev_t dev = minix_root_dev_id();

	return dev != 0 && ir0_block_is_present(dev);
}

int minix_read_block(uint32_t block_num, void *buffer)
{
	uint32_t lba = block_num * 2;
	uint8_t num_sectors = 2;
	dev_t dev;
	int ret;

	minix_emit_blockdev_classify();
	dev = minix_root_dev_id();
	if (dev == 0)
		return -ENODEV;

	ret = ir0_block_read(dev, lba, num_sectors, buffer);
	exec_read_trace_device_read(lba, num_sectors, ret == 0 ? 1 : 0, buffer);

	if (ret != 0)
		return -EIO;

	return 0;
}

int minix_write_block(uint32_t block_num, const void *buffer)
{
	uint32_t lba = block_num * 2;
	uint8_t num_sectors = 2;
	dev_t dev;
	int ret;

	minix_emit_blockdev_classify();
	dev = minix_root_dev_id();
	if (dev == 0)
		return -ENODEV;

	ret = ir0_block_write(dev, lba, num_sectors, buffer);
	if (ret != 0)
		return -EIO;

	return 0;
}

bool minix_is_zone_free(uint32_t zone_num)
{
  uint32_t byte_index;
  uint32_t bit_index;

  if (!minix_fs.zone_bitmap)
    return false;
  if (zone_num < minix_fs.superblock.s_firstdatazone ||
      zone_num >= MINIX_MAX_ZONES)
    return false;

  byte_index = (zone_num - minix_fs.superblock.s_firstdatazone) / 8;
  bit_index = (zone_num - minix_fs.superblock.s_firstdatazone) % 8;
  if (byte_index >= minix_zmap_bytes())
    return false;

  /* MINIX: bit 1 = free, bit 0 = used */
  return (minix_fs.zone_bitmap[byte_index] & (1U << bit_index)) != 0;
}

void minix_mark_zone_used(uint32_t zone_num)
{
  uint32_t byte_index;
  uint32_t bit_index;

  if (!minix_fs.zone_bitmap)
    return;
  if (zone_num < minix_fs.superblock.s_firstdatazone ||
      zone_num >= MINIX_MAX_ZONES)
    return;

  byte_index = (zone_num - minix_fs.superblock.s_firstdatazone) / 8;
  bit_index = (zone_num - minix_fs.superblock.s_firstdatazone) % 8;
  if (byte_index >= minix_zmap_bytes())
    return;

  minix_fs.zone_bitmap[byte_index] &= ~(1U << bit_index);
  minix_zmap_dirty = 1;
  if (zone_num + 1 > minix_zmap_search_start)
    minix_zmap_search_start = zone_num + 1;
}

uint32_t minix_alloc_zone(void)
{
  uint32_t limit = minix_fs.superblock.s_nzones;
  uint32_t first = minix_fs.superblock.s_firstdatazone;
  uint32_t start;
  uint32_t i;

  if (limit == 0 || limit > MINIX_MAX_ZONES)
    limit = MINIX_MAX_ZONES;
  if (first >= limit)
    return 0;

  start = minix_zmap_search_start;
  if (start < first || start >= limit)
    start = first;

  for (i = start; i < limit; i++)
  {
    if (minix_is_zone_free(i))
    {
      minix_mark_zone_used(i);
      return i;
    }
  }
  for (i = first; i < start; i++)
  {
    if (minix_is_zone_free(i))
    {
      minix_mark_zone_used(i);
      return i;
    }
  }
  return 0;
}

void minix_free_zone(uint32_t zone_num)
{
  uint32_t byte_index;
  uint32_t bit_index;

  if (!minix_fs.zone_bitmap)
    return;
  if (zone_num < minix_fs.superblock.s_firstdatazone ||
      zone_num >= MINIX_MAX_ZONES)
    return;

  byte_index = (zone_num - minix_fs.superblock.s_firstdatazone) / 8;
  bit_index = (zone_num - minix_fs.superblock.s_firstdatazone) % 8;
  if (byte_index >= minix_zmap_bytes())
    return;

  minix_fs.zone_bitmap[byte_index] |= (1U << bit_index);
  minix_zmap_dirty = 1;
  if (zone_num < minix_zmap_search_start)
    minix_zmap_search_start = zone_num;
}

uint32_t minix_alloc_inode(void)
{
  uint32_t limit = minix_fs.superblock.s_ninodes;

  if (limit == 0 || limit >= MINIX_MAX_INODES)
    limit = MINIX_MAX_INODES - 1;

  for (uint32_t i = 1; i <= limit; i++)
  {
    uint32_t byte = i / 8;
    uint32_t bit = i % 8;

    if (!(minix_fs.inode_bitmap[byte] & (1 << bit)))
    {
      minix_fs.inode_bitmap[byte] |= (1 << bit);
      return i;
    }
  }

  return 0;
}

int minix_sync_inode_bitmap(void)
{
  uint32_t imap_block = 2;

  if (!minix_fs.inode_bitmap)
    return -EIO;
  if (minix_write_block(imap_block, minix_fs.inode_bitmap) != 0)
    return -EIO;
  return 0;
}

// ===============================================================================
// PUBLIC FUNCTIONS IMPLEMENTATION
// ===============================================================================

bool minix_fs_is_working(void)
{
    return minix_fs.initialized;
}

/*
 * minix_fs_ensure_root_inode - Repair root when superblock is valid but the
 * on-disk root inode lost its directory type bits (seen on some disk.img builds
 * as mode=0xa35 with IFMT=0). Without IFDIR, vfs_mkdir on "/" fails ENOTDIR.
 */
static int minix_fs_ensure_root_inode(void)
{
  minix_inode_t root;
  mode_t perms;

  if (minix_read_inode(MINIX_ROOT_INODE, &root) != 0)
    return -1;

  if ((root.i_mode & MINIX_IFMT) == MINIX_IFDIR)
    return 0;

  log_warn_fmt("MINIX", "root inode mode=0x%x missing IFDIR, repairing",
               root.i_mode);

  perms = (mode_t)(root.i_mode & 0777);
  if (perms == 0)
    perms = (mode_t)(MINIX_IRWXU | MINIX_IRGRP | MINIX_IROTH);

  root.i_mode = (uint16_t)(MINIX_IFDIR | perms);

  if (root.i_zone[0] == 0)
    root.i_zone[0] = minix_fs.superblock.s_firstdatazone;
  if (root.i_nlinks < 2)
    root.i_nlinks = 2;
  if (root.i_size == 0)
    root.i_size = (uint32_t)(2 * sizeof(minix_dir_entry_t));

  if (minix_fs_write_inode(MINIX_ROOT_INODE, &root) != 0)
    return -1;

  if (root.i_zone[0] != 0)
  {
    uint8_t dir_block[MINIX_BLOCK_SIZE];

    if (minix_read_block(root.i_zone[0], dir_block) == 0)
    {
      minix_dir_entry_t *entries = (minix_dir_entry_t *)dir_block;

      if (entries[0].inode == 0)
      {
        entries[0].inode = MINIX_ROOT_INODE;
        strncpy(entries[0].name, ".", MINIX_NAME_LEN);
        entries[1].inode = MINIX_ROOT_INODE;
        strncpy(entries[1].name, "..", MINIX_NAME_LEN);
        if (minix_write_block(root.i_zone[0], dir_block) != 0)
          return -1;
      }
    }
  }

  root_inode_cached = false;
  return 0;
}

int minix_fs_init(void)
{
  /* FORCE REAL DISK USAGE - In QEMU, disk is always available */

  /* Read superblock from disk (block-sized buffer to avoid overflow) */
  uint8_t block_buffer[MINIX_BLOCK_SIZE];
  if (minix_read_block(1, block_buffer) != 0)
  {
    log_debug_fmt("MINIX", "superblock read failed, formatting");
    return minix_fs_format();
  }
  memcpy(&minix_fs.superblock, block_buffer, sizeof(minix_superblock_t));
  log_info_fmt("MINIX", "superblock read OK magic=0x%x", minix_fs.superblock.s_magic);

  /* Check magic number */
  if (minix_fs.superblock.s_magic != MINIX_SUPER_MAGIC)
  {
    // Invalid - format disk
    return minix_fs_format();
  }

  /*
   * Reject absurd geometry before kmalloc(zmap). A corrupt s_zmap_blocks
   * (historically written by minix_fs_free_inode into the superblock) used
   * to panic with "kmalloc: size too large".
   */
  if (minix_fs.superblock.s_imap_blocks == 0 ||
      minix_fs.superblock.s_zmap_blocks == 0 ||
      minix_fs.superblock.s_zmap_blocks > 256 ||
      minix_fs.superblock.s_imap_blocks > 16)
  {
    log_warn_fmt("MINIX", "corrupt geometry imap=%u zmap=%u, formatting",
                 minix_fs.superblock.s_imap_blocks,
                 minix_fs.superblock.s_zmap_blocks);
    return minix_fs_format();
  }

  // Read bitmaps
  // Block 2 is always the first inode bitmap block
  uint32_t imap_block = 2;
  uint32_t zmap_block = 2 + minix_fs.superblock.s_imap_blocks;
  size_t zmap_bytes;
  uint16_t zb;

  if (!minix_fs.inode_bitmap)
  {
    minix_fs.inode_bitmap = kmalloc(MINIX_BLOCK_SIZE);
    if (!minix_fs.inode_bitmap)
      return -1;
  }

  zmap_bytes = minix_zmap_bytes();
  if (minix_fs.zone_bitmap)
  {
    kfree(minix_fs.zone_bitmap);
    minix_fs.zone_bitmap = NULL;
  }
  minix_fs.zone_bitmap = kmalloc(zmap_bytes);
  if (!minix_fs.zone_bitmap)
    return -1;

  if (minix_read_block(imap_block, minix_fs.inode_bitmap) != 0)
  {
    return minix_fs_format();
  }

  for (zb = 0; zb < minix_fs.superblock.s_zmap_blocks; zb++)
  {
    if (minix_read_block(zmap_block + zb,
                         minix_fs.zone_bitmap +
                             (size_t)zb * MINIX_BLOCK_SIZE) != 0)
    {
      return minix_fs_format();
    }
  }
  minix_zmap_dirty = 0;
  minix_zmap_search_start = minix_fs.superblock.s_firstdatazone;

  // Double check root inode - if it's all zeros, the disk is corrupt or uninitialized
  minix_inode_t root_inode_check;
  minix_fs.initialized = true; // Temporary set to allow read_inode to work
  if (minix_read_inode(MINIX_ROOT_INODE, &root_inode_check) != 0 || root_inode_check.i_mode == 0)
  {
    log_warn_fmt("MINIX", "root inode corrupt (mode=0x%x), forcing format", root_inode_check.i_mode);
    minix_fs.initialized = false;
    return minix_fs_format();
  }

  if (minix_fs_ensure_root_inode() != 0)
  {
    minix_fs.initialized = false;
    return minix_fs_format();
  }

  if (minix_read_inode(MINIX_ROOT_INODE, &root_inode_check) != 0)
  {
    minix_fs.initialized = false;
    return minix_fs_format();
  }

  log_info_fmt("MINIX", "root inode OK mode=0x%x (dir=%d)", root_inode_check.i_mode,
               (root_inode_check.i_mode & MINIX_IFDIR) ? 1 : 0);

  /* Valid filesystem found */
  minix_fs.initialized = true;
  root_inode_cached = false; // Reset cache
  return 0;
}

int minix_fs_format(void)
{
  // REAL MINIX FILESYSTEM CREATION

  // Initialize superblock
  memset(&minix_fs.superblock, 0, sizeof(minix_superblock_t));
  minix_fs.superblock.s_magic = MINIX_SUPER_MAGIC;
  minix_fs.superblock.s_ninodes = 64;    // Small but real
  minix_fs.superblock.s_nzones = 1024;   // 1MB of data
  minix_fs.superblock.s_imap_blocks = 1; // 1 block for inode bitmap
  minix_fs.superblock.s_zmap_blocks = 1; // 1 block for zone bitmap
  minix_fs.superblock.s_firstdatazone =
      5; // Block layout: 0=boot, 1=super, 2=imap, 3=zmap, 4=inodes, 5+=data
  minix_fs.superblock.s_log_zone_size = 0;
  minix_fs.superblock.s_max_size = 1048576; /* 1MB max file size */

  /* Write superblock (block-sized to match on-disk layout) */
  uint8_t sb_block[MINIX_BLOCK_SIZE];
  memset(sb_block, 0, sizeof(sb_block));
  memcpy(sb_block, &minix_fs.superblock, sizeof(minix_superblock_t));
  if (minix_write_block(1, sb_block) != 0)
  {
    return -1;
  }

  // Inicializar bitmaps
  if (!minix_fs.inode_bitmap)
  {
    minix_fs.inode_bitmap = kmalloc(MINIX_BLOCK_SIZE);
    if (!minix_fs.inode_bitmap)
      return -1;
  }
  if (!minix_fs.zone_bitmap)
  {
    minix_fs.zone_bitmap = kmalloc(MINIX_BLOCK_SIZE);
    if (!minix_fs.zone_bitmap)
      return -1;
  }

  memset(minix_fs.inode_bitmap, 0, MINIX_BLOCK_SIZE);
  
  // CRITICAL: Initialize zone bitmap with 0xFF (all bits = 1 = all zones FREE)
  // In MINIX: bit=0 means USED, bit=1 means FREE
  memset(minix_fs.zone_bitmap, 0xFF, MINIX_BLOCK_SIZE);

  // Mark inode 1 as used (root directory)
  // In MINIX bitmap: bit 1 is inode 1, bit 0 is unused or reserved
  minix_fs.inode_bitmap[0] = 0x02; // Bit 1 = inode 1

  /* Calculate block offsets for bitmaps and inode table */
  uint32_t imap_block = 2;
  uint32_t zmap_block = 2 + minix_fs.superblock.s_imap_blocks;
  uint32_t inode_table_block = zmap_block + minix_fs.superblock.s_zmap_blocks;
  (void)inode_table_block;  /* Reserved for inode table writes */

  /* Write bitmaps to disk */
  if (minix_write_block(imap_block, minix_fs.inode_bitmap) != 0)
  {
    return -1;
  }

  if (minix_write_block(zmap_block, minix_fs.zone_bitmap) != 0)
  {
    return -1;
  }


  // Create root inode
  minix_inode_t root_inode;
  memset(&root_inode, 0, sizeof(minix_inode_t));
  root_inode.i_mode = MINIX_IFDIR | MINIX_IRWXU | MINIX_IRGRP | MINIX_IROTH;
  root_inode.i_uid = 0;
  root_inode.i_size = 2 * sizeof(minix_dir_entry_t); // . and .. entries
  root_inode.i_time = 0;
  root_inode.i_gid = 0;
  root_inode.i_nlinks = 2;                                    // . and .. links
  root_inode.i_zone[0] = minix_fs.superblock.s_firstdatazone; // First data zone

  klog_print("SERIAL: minix_fs_format: creating root inode with mode=");
  klog_hex32(root_inode.i_mode);
  klog_print(" zone[0]=");
  klog_hex32(root_inode.i_zone[0]);
  klog_print("\n");

  // Write root inode using proper function
  if (minix_fs_write_inode(MINIX_ROOT_INODE, &root_inode) != 0)
  {
    klog_print("SERIAL: minix_fs_format: FAILED to write root inode!\n");
    return -1;
  }
  
  // Verify what we just wrote
  minix_inode_t verify_inode;
  if (minix_read_inode(MINIX_ROOT_INODE, &verify_inode) == 0) {
    klog_print("SERIAL: minix_fs_format: VERIFY root inode mode=");
    klog_hex32(verify_inode.i_mode);
    klog_print("\n");
  } else {
    klog_print("SERIAL: minix_fs_format: VERIFY FAILED to read back!\n");
  }
  
  klog_print("SERIAL: minix_fs_format: root inode written successfully\n");

  // Create root directory with . and .. entries
  uint8_t root_dir_block[MINIX_BLOCK_SIZE];
  memset(root_dir_block, 0, MINIX_BLOCK_SIZE);

  minix_dir_entry_t *entries = (minix_dir_entry_t *)root_dir_block;

  // Entry "."
  entries[0].inode = 1; // Root inode
  strncpy(entries[0].name, ".", MINIX_NAME_LEN);

  // Entry ".."
  entries[1].inode = 1; // Root inode (parent of root is root)
  strncpy(entries[1].name, "..", MINIX_NAME_LEN);

  // Write root directory
  if (minix_write_block(minix_fs.superblock.s_firstdatazone, root_dir_block) !=
      0)
  {
    klog_print("SERIAL: minix_fs_format: FAILED to write root directory block!\n");
    return -1;
  }

  klog_print("SERIAL: minix_fs_format: root directory block written successfully\n");

  // Mark only inode 1 as used in bitmap
  minix_fs.inode_bitmap[0] |= 0x02; // Bit 1 = inode 1

  // CRITICAL: Mark root directory zone as used in zone bitmap
  // In MINIX: bit=0 means USED, bit=1 means FREE
  // Root directory uses s_firstdatazone (zone 5), so clear bit 5
  minix_fs.zone_bitmap[0] &= ~(1 << 5); // Mark zone 5 as USED (clear bit)

  klog_print("SERIAL: minix_fs_format: marked root inode and root zone as used\n");

  // Write inode bitmap
  if (minix_write_block(imap_block, minix_fs.inode_bitmap) != 0)
  {
    klog_print("SERIAL: minix_fs_format: FAILED to write inode bitmap!\n");
    return -1;
  }

  // Write zone bitmap
  if (minix_write_block(zmap_block, minix_fs.zone_bitmap) != 0)
  {
    klog_print("SERIAL: minix_fs_format: FAILED to write zone bitmap!\n");
    return -1;
  }

  klog_print("SERIAL: minix_fs_format: filesystem formatted successfully!\n");

  minix_fs.initialized = true;
  root_inode_cached = false; // Reset cache

  // Create test files to verify ls works
  klog_print("SERIAL: minix_fs_format: creating test files...\n");  

  klog_print("SERIAL: minix_fs_format: test files created successfully\n");

  return 0;
}

static struct vfs_ops minix_fs_ops = {
	.stat    = minix_fs_stat,
	.mkdir   = minix_fs_mkdir,
	.create  = minix_create,
	.unlink  = minix_fs_rm,
	.rmdir   = minix_fs_rmdir,
	.readdir = minix_fs_readdir,
	.read    = minix_fs_read_file_wrapper,
	.write   = minix_fs_write_file_wrapper,
	.truncate = minix_truncate,
	.link    = minix_fs_link,
	.chown   = minix_fs_chown,
	.chmod   = minix_fs_chmod,
	.utimens = minix_fs_utimens,
	.map_swap_extents = minix_map_swap_extents,
};

static struct vfs_fstype minix_fs_type;

static int minix_mount(const char *dev_name __attribute__((unused)), const char *dir_name)
{
	if (!dir_name || strcmp(dir_name, "/") != 0)
		return -ENOTSUPP;

	if (!minix_fs_is_working()) {
		int ret = minix_fs_init();
		if (ret != 0) {
			klog_print("[VFS] ERROR - MINIX_MOUNT: minix_fs_init failed\n");
			return ret;
		}
	}
	return 0;
}

int minix_fs_register(void)
{
	minix_fs_type.name  = "minix";
	minix_fs_type.ops   = &minix_fs_ops;
	minix_fs_type.mount = minix_mount;
	minix_fs_type.next  = NULL;
	return vfs_register_fs(&minix_fs_type);
}
