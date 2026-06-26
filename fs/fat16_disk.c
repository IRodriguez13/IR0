/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fat16_disk.c
 * Description: IR0 — read-only FAT16 parser on ir0 block_dev (hda, hda1, …)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "fat16_disk.h"
#include "vfs.h"
#include <ir0/block_dev.h>
#include <ir0/partition.h>
#include <ir0/kmem.h>
#include <ir0/errno.h>
#include <ir0/stat.h>
#include <string.h>

#define FAT16_MAX_MOUNTS      4
#define FAT16_MAX_PATH        256
#define FAT16_MAX_NAME          12
#define FAT16_ROOT_MAX        512
#define FAT16_CLUSTER_EOF   0xFFF8U

struct fat16_vol
{
	int in_use;
	char blk_name[16];
	char mount_path[FAT16_MAX_PATH];
	uint32_t base_lba;
	uint16_t bytes_per_sector;
	uint8_t sectors_per_cluster;
	uint16_t reserved_sectors;
	uint8_t num_fats;
	uint16_t root_entry_count;
	uint16_t fat_sectors;
	uint32_t root_dir_sectors;
	uint32_t first_data_sector;
	uint32_t total_sectors;
	uint32_t data_clusters;
};

static struct fat16_vol g_vols[FAT16_MAX_MOUNTS];

struct fat16_dirent
{
	uint8_t name[11];
	uint8_t attr;
	uint8_t reserved;
	uint8_t ctime_tenth;
	uint16_t ctime;
	uint16_t cdate;
	uint16_t adate;
	uint16_t cluster_hi;
	uint16_t mtime;
	uint16_t mdate;
	uint16_t cluster_lo;
	uint32_t size;
} __attribute__((packed));

#define FAT_ATTR_DIR   0x10U
#define FAT_ATTR_LFN   0x0FU
#define FAT_ATTR_MASK  (FAT_ATTR_DIR | 0x01U | 0x02U | 0x04U)

static struct fat16_vol *fat16_find_mount(const char *vfs_path, char *rel, size_t rel_sz,
					  int *is_root)
{
	size_t best = 0;
	struct fat16_vol *best_vol = NULL;

	if (!vfs_path || !rel || rel_sz == 0)
		return NULL;

	for (int i = 0; i < FAT16_MAX_MOUNTS; i++)
	{
		struct fat16_vol *v = &g_vols[i];
		size_t mlen;

		if (!v->in_use)
			continue;
		mlen = strlen(v->mount_path);
		if (strncmp(vfs_path, v->mount_path, mlen) != 0)
			continue;
		if (vfs_path[mlen] != '\0' && vfs_path[mlen] != '/')
			continue;
		if (mlen >= best)
		{
			best = mlen;
			best_vol = v;
		}
	}

	if (!best_vol)
		return NULL;

	if (vfs_path[best] == '\0')
	{
		rel[0] = '\0';
		*is_root = 1;
		return best_vol;
	}

	if (vfs_path[best] == '/')
		best++;

	{
		size_t rlen = strlen(vfs_path + best);

		if (rlen >= rel_sz)
			return NULL;
		memcpy(rel, vfs_path + best, rlen + 1);
	}
	*is_root = 0;
	return best_vol;
}

static int fat16_read_sectors(struct fat16_vol *v, uint32_t rel_lba, uint8_t count,
			      void *buf)
{
	if (!v || !buf || count == 0)
		return -EINVAL;
	if (!block_dev_read_sectors(v->blk_name, v->base_lba + rel_lba, count, buf))
		return -EIO;
	return 0;
}

static int fat16_read_sector(struct fat16_vol *v, uint32_t rel_lba, void *buf)
{
	return fat16_read_sectors(v, rel_lba, 1, buf);
}

int fat16_disk_probe_bpb(const uint8_t sector[512], uint16_t *bytes_per_sector_out)
{
	uint16_t bps;
	uint16_t fat_sectors;
	uint16_t root_cnt;
	uint32_t root_secs;
	uint32_t data_secs;
	uint32_t total_sec;

	if (!sector)
		return -EINVAL;

	if (sector[510] != 0x55 || sector[511] != 0xAA)
		return -EINVAL;

	bps = (uint16_t)sector[0x0B] | ((uint16_t)sector[0x0C] << 8);
	if (bps != 512)
		return -ENOTSUPP;

	if (memcmp(&sector[0x36], "FAT16   ", 8) != 0 &&
	    memcmp(&sector[0x36], "FAT     ", 8) != 0)
		return -ENOTSUPP;

	fat_sectors = (uint16_t)sector[0x16] | ((uint16_t)sector[0x17] << 8);
	if (fat_sectors == 0)
	{
		fat_sectors = (uint16_t)sector[0x24] | ((uint16_t)sector[0x25] << 8);
		if (fat_sectors == 0)
			return -EINVAL;
	}

	root_cnt = (uint16_t)sector[0x11] | ((uint16_t)sector[0x12] << 8);
	root_secs = ((uint32_t)root_cnt * 32U + (uint32_t)bps - 1U) / (uint32_t)bps;

	total_sec = (uint16_t)sector[0x13] | ((uint16_t)sector[0x14] << 8);
	if (total_sec == 0)
		total_sec = (uint32_t)sector[0x20] | ((uint32_t)sector[0x21] << 8) |
			    ((uint32_t)sector[0x22] << 16) | ((uint32_t)sector[0x23] << 24);

	data_secs = total_sec - (uint32_t)sector[0x0E] -
		    (uint32_t)sector[0x10] * (uint32_t)fat_sectors - root_secs;
	if (data_secs == 0)
		return -EINVAL;

	if (bytes_per_sector_out)
		*bytes_per_sector_out = bps;
	return 0;
}

static int fat16_load_bpb(struct fat16_vol *v, const uint8_t *sector)
{
	uint16_t total16;
	uint32_t total32;
	uint32_t root_secs;

	if (fat16_disk_probe_bpb(sector, &v->bytes_per_sector) != 0)
		return -EINVAL;

	v->sectors_per_cluster = sector[0x0D];
	if (v->sectors_per_cluster == 0 || (v->sectors_per_cluster & (v->sectors_per_cluster - 1)) != 0)
		return -EINVAL;

	v->reserved_sectors = (uint16_t)sector[0x0E] | ((uint16_t)sector[0x0F] << 8);
	v->num_fats = sector[0x10];
	if (v->num_fats == 0)
		return -EINVAL;

	v->root_entry_count = (uint16_t)sector[0x11] | ((uint16_t)sector[0x12] << 8);
	v->fat_sectors = (uint16_t)sector[0x16] | ((uint16_t)sector[0x17] << 8);
	if (v->fat_sectors == 0)
		v->fat_sectors = (uint16_t)sector[0x24] | ((uint16_t)sector[0x25] << 8);

	total16 = (uint16_t)sector[0x13] | ((uint16_t)sector[0x14] << 8);
	total32 = (uint32_t)sector[0x20] | ((uint32_t)sector[0x21] << 8) |
		  ((uint32_t)sector[0x22] << 16) | ((uint32_t)sector[0x23] << 24);
	v->total_sectors = total16 ? total16 : total32;

	root_secs = ((uint32_t)v->root_entry_count * 32U + 511U) / 512U;
	v->root_dir_sectors = root_secs;
	v->first_data_sector = (uint32_t)v->reserved_sectors +
			       (uint32_t)v->num_fats * (uint32_t)v->fat_sectors +
			       root_secs;

	{
		uint32_t data_secs = v->total_sectors - v->first_data_sector;

		v->data_clusters = data_secs / (uint32_t)v->sectors_per_cluster;
	}
	return 0;
}

static int fat16_parse_dev(const char *dev, char *blk_out, size_t blk_sz,
			   uint32_t *part_lba_out, int *use_whole_disk)
{
	const char *name;
	uint8_t disk_id;
	char letter;

	if (!dev || !blk_out || !part_lba_out || !use_whole_disk)
		return -EINVAL;

	(void)blk_sz;

	name = dev;
	if (strncmp(dev, "/dev/", 5) == 0)
		name = dev + 5;

	if (strlen(name) < 3 || name[0] != 'h' || name[1] != 'd')
		return -EINVAL;

	letter = name[2];
	if (letter < 'a' || letter > 'd')
		return -EINVAL;
	disk_id = (uint8_t)(letter - 'a');

	if (name[3] == '\0')
	{
		*use_whole_disk = 1;
		blk_out[0] = 'h';
		blk_out[1] = 'd';
		blk_out[2] = letter;
		blk_out[3] = '\0';
		*part_lba_out = 0;
		return 0;
	}

	if (name[3] >= '1' && name[3] <= '9' && name[4] == '\0')
	{
		partition_info_t pi;
		uint8_t part_idx = (uint8_t)(name[3] - '1');

		if (get_partition_info(disk_id, part_idx, &pi) != 0)
			return -ENODEV;
		*use_whole_disk = 0;
		blk_out[0] = 'h';
		blk_out[1] = 'd';
		blk_out[2] = letter;
		blk_out[3] = '\0';
		*part_lba_out = (uint32_t)pi.start_lba;
		return 0;
	}

	return -EINVAL;
}

static int fat16_find_fat16_partition(uint8_t disk_id, uint32_t *lba_out)
{
	uint8_t mbr[512];
	int i;

	if (!block_dev_read_sectors(block_dev_legacy_name(disk_id), 0, 1, mbr))
		return -EIO;
	if (mbr[510] != 0x55 || mbr[511] != 0xAA)
		return -ENOENT;

	for (i = 0; i < 4; i++)
	{
		const uint8_t *ent = &mbr[0x1BE + i * 16];
		uint8_t sys = ent[4];
		uint32_t start = (uint32_t)ent[8] | ((uint32_t)ent[9] << 8) |
				 ((uint32_t)ent[10] << 16) | ((uint32_t)ent[11] << 24);

		if (sys == 0x04 || sys == 0x06 || sys == 0x0E)
		{
			*lba_out = start;
			return 0;
		}
	}
	return -ENOENT;
}

static void fat16_format_dirent_name(const struct fat16_dirent *de, char *out, size_t out_sz)
{
	char base[9];
	char ext[4];
	int bi = 0;
	int ei = 0;
	int i;

	for (i = 0; i < 8; i++)
	{
		if (de->name[i] == ' ')
			break;
		base[bi++] = (char)de->name[i];
	}
	base[bi] = '\0';

	for (i = 8; i < 11; i++)
	{
		if (de->name[i] == ' ')
			break;
		ext[ei++] = (char)de->name[i];
	}
	ext[ei] = '\0';

	if (ext[0])
	{
		size_t bl = strlen(base);
		size_t el = strlen(ext);

		if (bl + 1 + el >= out_sz)
			return;
		memcpy(out, base, bl);
		out[bl] = '.';
		memcpy(out + bl + 1, ext, el + 1);
	}
	else
	{
		strncpy(out, base, out_sz - 1);
		out[out_sz - 1] = '\0';
	}
}

static int fat16_name_match(const char *want, const struct fat16_dirent *de)
{
	char formatted[FAT16_MAX_NAME];

	if (de->name[0] == 0x00 || de->name[0] == (uint8_t)0xE5)
		return 0;
	if (de->attr == FAT_ATTR_LFN)
		return 0;
	fat16_format_dirent_name(de, formatted, sizeof(formatted));
	return strncmp(want, formatted, FAT16_MAX_NAME) == 0;
}

static int fat16_read_root(struct fat16_vol *v, struct fat16_dirent *out, int max,
			   int *count_out)
{
	uint8_t *buf;
	uint32_t root_bytes;
	int entries;
	int ret;

	if (!out || max <= 0 || !count_out)
		return -EINVAL;

	root_bytes = v->root_dir_sectors * 512U;
	buf = (uint8_t *)kmalloc_try(root_bytes);
	if (!buf)
		return -ENOMEM;

	ret = fat16_read_sectors(v, v->reserved_sectors + (uint32_t)v->num_fats * v->fat_sectors,
				 (uint8_t)v->root_dir_sectors, buf);
	if (ret != 0)
	{
		kfree(buf);
		return ret;
	}

	entries = (int)(root_bytes / 32U);
	if (entries > max)
		entries = max;
	memcpy(out, buf, (size_t)entries * 32U);
	kfree(buf);
	*count_out = entries;
	return 0;
}

static int fat16_find_in_root(struct fat16_vol *v, const char *name,
			      struct fat16_dirent *de_out)
{
	struct fat16_dirent root[FAT16_ROOT_MAX];
	int count = 0;
	int ret;
	int i;

	ret = fat16_read_root(v, root, FAT16_ROOT_MAX, &count);
	if (ret != 0)
		return ret;

	for (i = 0; i < count; i++)
	{
		if (root[i].name[0] == 0x00)
			break;
		if (fat16_name_match(name, &root[i]))
		{
			*de_out = root[i];
			return 0;
		}
	}
	return -ENOENT;
}

static uint16_t fat16_next_cluster(struct fat16_vol *v, uint16_t cluster)
{
	uint32_t fat_offset;
	uint32_t fat_sector;
	uint32_t ent_offset;
	uint8_t sec[512];
	uint16_t next;
	int ret;

	fat_offset = (uint32_t)cluster * 2U;
	fat_sector = v->reserved_sectors + fat_offset / 512U;
	ent_offset = fat_offset % 512U;
	ret = fat16_read_sector(v, fat_sector, sec);
	if (ret != 0)
		return 0;
	next = (uint16_t)sec[ent_offset] | ((uint16_t)sec[ent_offset + 1] << 8);
	return next;
}

static int fat16_read_file(struct fat16_vol *v, const struct fat16_dirent *de,
			   void *buf, size_t count, size_t *nread, off_t offset)
{
	uint16_t cluster;
	uint32_t cluster_bytes;
	uint8_t *cluster_buf;
	size_t done = 0;
	size_t file_size = de->size;
	size_t skip = (size_t)offset;
	int ret = 0;

	if (!buf || !nread)
		return -EINVAL;
	*nread = 0;
	if ((size_t)offset >= file_size)
		return 0;
	if (count > file_size - (size_t)offset)
		count = file_size - (size_t)offset;

	cluster = de->cluster_lo;
	cluster_bytes = (uint32_t)v->sectors_per_cluster * 512U;
	cluster_buf = (uint8_t *)kmalloc_try(cluster_bytes);
	if (!cluster_buf)
		return -ENOMEM;

	while (cluster >= 2 && cluster < FAT16_CLUSTER_EOF && done < count)
	{
		uint32_t rel = v->first_data_sector + (uint32_t)(cluster - 2) *
			       (uint32_t)v->sectors_per_cluster;
		size_t coff;

		ret = fat16_read_sectors(v, rel, v->sectors_per_cluster, cluster_buf);
		if (ret != 0)
			break;

		for (coff = 0; coff < cluster_bytes && done < count; coff++)
		{
			if (skip > 0)
			{
				skip--;
				continue;
			}
			((uint8_t *)buf)[done++] = cluster_buf[coff];
		}
		cluster = fat16_next_cluster(v, cluster);
	}

	kfree(cluster_buf);
	if (ret != 0)
		return ret;
	*nread = done;
	return 0;
}

int fat16_disk_mount(const char *dev, const char *mount_dir)
{
	struct fat16_vol *slot = NULL;
	char blk[16];
	uint32_t part_lba = 0;
	int whole = 0;
	uint8_t boot[512];
	int ret;
	char mount_norm[FAT16_MAX_PATH];
	uint8_t disk_id;

	if (!dev || !mount_dir)
		return -EINVAL;

	for (int i = 0; i < FAT16_MAX_MOUNTS; i++)
	{
		if (!g_vols[i].in_use)
		{
			slot = &g_vols[i];
			break;
		}
	}
	if (!slot)
		return -ENOSPC;

	ret = fat16_parse_dev(dev, blk, sizeof(blk), &part_lba, &whole);
	if (ret != 0)
		return ret;
	if (!block_dev_is_present(blk))
		return -ENXIO;

	disk_id = (uint8_t)(blk[2] - 'a');

	if (whole)
	{
		if (!block_dev_read_sectors(blk, 0, 1, boot))
			return -EIO;
		if (fat16_disk_probe_bpb(boot, NULL) != 0)
		{
			if (fat16_find_fat16_partition(disk_id, &part_lba) != 0)
				return -EINVAL;
			whole = 0;
		}
	}

	memset(slot, 0, sizeof(*slot));
	strncpy(slot->blk_name, blk, sizeof(slot->blk_name) - 1);
	slot->base_lba = part_lba;

	if (!block_dev_read_sectors(blk, part_lba, 1, boot))
		return -EIO;
	ret = fat16_load_bpb(slot, boot);
	if (ret != 0)
		return ret;

	strncpy(mount_norm, mount_dir, sizeof(mount_norm) - 1);
	mount_norm[sizeof(mount_norm) - 1] = '\0';
	if (mount_norm[0] != '/')
		return -EINVAL;
	strncpy(slot->mount_path, mount_norm, sizeof(slot->mount_path) - 1);
	slot->mount_path[sizeof(slot->mount_path) - 1] = '\0';
	slot->in_use = 1;
	return 0;
}

int fat16_disk_umount(const char *mount_dir)
{
	if (!mount_dir)
		return -EINVAL;
	for (int i = 0; i < FAT16_MAX_MOUNTS; i++)
	{
		if (g_vols[i].in_use && strcmp(g_vols[i].mount_path, mount_dir) == 0)
		{
			memset(&g_vols[i], 0, sizeof(g_vols[i]));
			return 0;
		}
	}
	return -ENOENT;
}

int fat16_disk_path_is_mounted(const char *vfs_path)
{
	char rel[FAT16_MAX_NAME];
	int is_root = 0;

	return fat16_find_mount(vfs_path, rel, sizeof(rel), &is_root) != NULL;
}

int fat16_disk_stat(const char *path, stat_t *buf)
{
	char rel[FAT16_MAX_PATH];
	int is_root = 0;
	struct fat16_vol *v;
	struct fat16_dirent de;
	int ret;

	if (!buf)
		return -EFAULT;
	v = fat16_find_mount(path, rel, sizeof(rel), &is_root);
	if (!v)
		return -ENODEV;
	memset(buf, 0, sizeof(*buf));
	if (is_root)
	{
		buf->st_mode = S_IFDIR | 0555;
		buf->st_nlink = 1;
		return 0;
	}
	ret = fat16_find_in_root(v, rel, &de);
	if (ret != 0)
		return ret;
	buf->st_mode = (de.attr & FAT_ATTR_DIR) ? (S_IFDIR | 0555) : (S_IFREG | 0444);
	buf->st_size = (off_t)de.size;
	buf->st_nlink = 1;
	return 0;
}

int fat16_disk_readdir(const char *path, struct vfs_dirent *entries, int max)
{
	char rel[FAT16_MAX_PATH];
	int is_root = 0;
	struct fat16_vol *v;
	struct fat16_dirent root[FAT16_ROOT_MAX];
	int count = 0;
	int out = 0;
	int ret;
	int i;

	if (!entries || max <= 0)
		return -EINVAL;
	v = fat16_find_mount(path, rel, sizeof(rel), &is_root);
	if (!v)
		return -ENODEV;
	if (!is_root)
		return -ENOTDIR;

	ret = fat16_read_root(v, root, FAT16_ROOT_MAX, &count);
	if (ret != 0)
		return ret;

	for (i = 0; i < count && out < max; i++)
	{
		if (root[i].name[0] == 0x00)
			break;
		if (root[i].attr == FAT_ATTR_LFN)
			continue;
		fat16_format_dirent_name(&root[i], entries[out].name,
					 sizeof(entries[out].name));
		entries[out].type = (root[i].attr & FAT_ATTR_DIR) ? DT_DIR : DT_REG;
		out++;
	}
	return out;
}

int fat16_disk_read(const char *path, void *buf, size_t count, size_t *bytes_read,
		    off_t offset)
{
	char rel[FAT16_MAX_PATH];
	int is_root = 0;
	struct fat16_vol *v;
	struct fat16_dirent de;
	int ret;

	if (!buf || !bytes_read)
		return -EINVAL;
	v = fat16_find_mount(path, rel, sizeof(rel), &is_root);
	if (!v)
		return -ENODEV;
	if (is_root)
		return -EISDIR;
	ret = fat16_find_in_root(v, rel, &de);
	if (ret != 0)
		return ret;
	if (de.attr & FAT_ATTR_DIR)
		return -EISDIR;
	return fat16_read_file(v, &de, buf, count, bytes_read, offset);
}

static int fat16_rofs(void)
{
	return -EROFS;
}

int fat16_disk_mkdir(const char *path, mode_t mode)
{
	(void)path;
	(void)mode;
	return fat16_rofs();
}

int fat16_disk_create(const char *path, mode_t mode)
{
	(void)path;
	(void)mode;
	return fat16_rofs();
}

int fat16_disk_unlink(const char *path)
{
	(void)path;
	return fat16_rofs();
}

int fat16_disk_rmdir(const char *path)
{
	(void)path;
	return fat16_rofs();
}

int fat16_disk_link(const char *oldpath, const char *newpath)
{
	(void)oldpath;
	(void)newpath;
	return -ENOSYS;
}

int fat16_disk_chown(const char *path, uid_t owner, gid_t group)
{
	(void)path;
	(void)owner;
	(void)group;
	return fat16_rofs();
}

int fat16_disk_chmod(const char *path, mode_t mode)
{
	(void)path;
	(void)mode;
	return fat16_rofs();
}

int fat16_disk_write(const char *path, const void *buf, size_t count,
		     size_t *bytes_written, off_t offset)
{
	(void)path;
	(void)buf;
	(void)count;
	(void)bytes_written;
	(void)offset;
	return fat16_rofs();
}

int fat16_disk_truncate(const char *path, size_t length)
{
	(void)path;
	(void)length;
	return fat16_rofs();
}
