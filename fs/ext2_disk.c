/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ext2_disk.c
 * Description: Bounded EXT2 backend for persistent IR0 volumes.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "ext2_disk.h"
#include "vfs.h"
#include <ir0/blockdev.h>
#include <ir0/clock.h>
#include <ir0/credentials.h>
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <ir0/klog.h>
#include <ir0/stat.h>
#include <string.h>

#define EXT2_MAGIC 0xEF53
#define EXT2_ROOT_INO 2
#define EXT2_MAX_MOUNTS 2
#define EXT2_MAX_PATH 256
#define EXT2_NAME_MAX 255
#define EXT2_NDIR_BLOCKS 12
#define EXT2_IND_BLOCK 12
#define EXT2_SUPER_OFFSET 1024
#define EXT2_FEATURE_INCOMPAT_FILETYPE 0x0002
#define EXT2_SUPPORTED_INCOMPAT EXT2_FEATURE_INCOMPAT_FILETYPE
#define EXT2_S_IFMT 0xF000
#define EXT2_S_IFREG 0x8000
#define EXT2_S_IFDIR 0x4000
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR 2

struct ext2_super
{
	uint32_t s_inodes_count;
	uint32_t s_blocks_count;
	uint32_t s_r_blocks_count;
	uint32_t s_free_blocks_count;
	uint32_t s_free_inodes_count;
	uint32_t s_first_data_block;
	uint32_t s_log_block_size;
	uint32_t s_log_frag_size;
	uint32_t s_blocks_per_group;
	uint32_t s_frags_per_group;
	uint32_t s_inodes_per_group;
	uint32_t s_mtime;
	uint32_t s_wtime;
	uint16_t s_mnt_count;
	uint16_t s_max_mnt_count;
	uint16_t s_magic;
	uint16_t s_state;
	uint16_t s_errors;
	uint16_t s_minor_rev_level;
	uint32_t s_lastcheck;
	uint32_t s_checkinterval;
	uint32_t s_creator_os;
	uint32_t s_rev_level;
	uint16_t s_def_resuid;
	uint16_t s_def_resgid;
	uint32_t s_first_ino;
	uint16_t s_inode_size;
	uint16_t s_block_group_nr;
	uint32_t s_feature_compat;
	uint32_t s_feature_incompat;
	uint32_t s_feature_ro_compat;
} __attribute__((packed));

struct ext2_bgd
{
	uint32_t bg_block_bitmap;
	uint32_t bg_inode_bitmap;
	uint32_t bg_inode_table;
	uint16_t bg_free_blocks_count;
	uint16_t bg_free_inodes_count;
	uint16_t bg_used_dirs_count;
	uint16_t bg_pad;
	uint8_t bg_reserved[12];
} __attribute__((packed));

struct ext2_inode
{
	uint16_t i_mode;
	uint16_t i_uid;
	uint32_t i_size;
	uint32_t i_atime;
	uint32_t i_ctime;
	uint32_t i_mtime;
	uint32_t i_dtime;
	uint16_t i_gid;
	uint16_t i_links_count;
	uint32_t i_blocks;
	uint32_t i_flags;
	uint32_t i_osd1;
	uint32_t i_block[15];
	uint32_t i_generation;
	uint32_t i_file_acl;
	uint32_t i_dir_acl;
	uint32_t i_faddr;
	uint8_t i_osd2[12];
} __attribute__((packed));

struct ext2_dirent
{
	uint32_t inode;
	uint16_t rec_len;
	uint8_t name_len;
	uint8_t file_type;
} __attribute__((packed));

struct ext2_vol
{
	int in_use;
	char blk_name[16];
	char mount_path[EXT2_MAX_PATH];
	uint32_t block_size;
	uint32_t inodes_per_group;
	uint16_t inode_size;
	uint32_t first_data_block;
	uint32_t blocks_per_group;
	uint32_t blocks_count;
	uint32_t inodes_count;
	uint32_t inode_bitmap_block;
	uint32_t block_bitmap_block;
	uint32_t inode_table_block;
	uint32_t gd_block;
	struct ext2_super super;
	struct ext2_bgd bgd;
};

static struct ext2_vol g_vols[EXT2_MAX_MOUNTS];

static int ext2_allocate_block(struct ext2_vol *v, uint32_t *block);

static int ext2_read_block(struct ext2_vol *v, uint32_t block, void *buf)
{
	uint32_t sectors = v->block_size / 512;
	uint32_t lba = block * sectors;

	if (block >= v->blocks_count)
		return -EIO;
	if (ir0_block_read_by_name(v->blk_name, lba, sectors, buf))
		return -EIO;
	return 0;
}

static int ext2_write_block(struct ext2_vol *v, uint32_t block, const void *buf)
{
	uint32_t sectors = v->block_size / 512;
	uint32_t lba = block * sectors;

	if (block >= v->blocks_count)
		return -EIO;
	if (ir0_block_write_by_name(v->blk_name, lba, sectors, buf))
		return -EIO;
	return 0;
}

static int ext2_write_metadata(struct ext2_vol *v)
{
	uint8_t sector[1024];
	uint8_t *buf;
	int ret;

	if (ir0_block_read_by_name(v->blk_name, 2, 2, sector))
		return -EIO;
	memcpy(sector, &v->super, sizeof(v->super));
	if (ir0_block_write_by_name(v->blk_name, 2, 2, sector))
		return -EIO;
	buf = kmalloc(v->block_size);
	if (!buf)
		return -ENOMEM;
	ret = ext2_read_block(v, v->gd_block, buf);
	if (ret == 0)
	{
		memcpy(buf, &v->bgd, sizeof(v->bgd));
		ret = ext2_write_block(v, v->gd_block, buf);
	}
	kfree(buf);
	return ret;
}

static int ext2_read_inode(struct ext2_vol *v, uint32_t ino, struct ext2_inode *out)
{
	uint8_t *buf;
	uint32_t group, index, block, offset;
	int ret;

	if (ino == 0)
		return -EINVAL;
	if (ino > v->inodes_count)
		return -EINVAL;
	group = (ino - 1) / v->inodes_per_group;
	if (group != 0)
		return -EIO;
	index = (ino - 1) % v->inodes_per_group;
	block = v->inode_table_block + (index * v->inode_size) / v->block_size;
	offset = (index * v->inode_size) % v->block_size;

	buf = kmalloc(v->block_size);
	if (!buf)
		return -ENOMEM;
	ret = ext2_read_block(v, block, buf);
	if (ret == 0)
		memcpy(out, buf + offset, sizeof(*out));
	kfree(buf);
	return ret;
}

static int ext2_write_inode(struct ext2_vol *v, uint32_t ino,
			    const struct ext2_inode *in)
{
	uint8_t *buf;
	uint32_t index;
	uint32_t block;
	uint32_t offset;
	int ret;

	if (ino == 0 || ino > v->inodes_count)
		return -EINVAL;
	index = ino - 1;
	block = v->inode_table_block + (index * v->inode_size) / v->block_size;
	offset = (index * v->inode_size) % v->block_size;
	buf = kmalloc(v->block_size);
	if (!buf)
		return -ENOMEM;
	ret = ext2_read_block(v, block, buf);
	if (ret == 0)
	{
		memcpy(buf + offset, in, sizeof(*in));
		ret = ext2_write_block(v, block, buf);
	}
	kfree(buf);
	return ret;
}

static uint32_t ext2_max_file_blocks(const struct ext2_vol *v)
{
	return EXT2_NDIR_BLOCKS + v->block_size / sizeof(uint32_t);
}

static int ext2_inode_block(struct ext2_vol *v, const struct ext2_inode *inode,
			    uint32_t index, uint32_t *block)
{
	uint32_t *table;
	int ret;

	if (index < EXT2_NDIR_BLOCKS)
	{
		*block = inode->i_block[index];
		return 0;
	}
	index -= EXT2_NDIR_BLOCKS;
	if (index >= v->block_size / sizeof(uint32_t))
		return -EFBIG;
	if (inode->i_block[EXT2_IND_BLOCK] == 0)
	{
		*block = 0;
		return 0;
	}
	table = kmalloc(v->block_size);
	if (!table)
		return -ENOMEM;
	ret = ext2_read_block(v, inode->i_block[EXT2_IND_BLOCK], table);
	if (ret == 0)
		*block = table[index];
	kfree(table);
	return ret;
}

static int ext2_inode_ensure_block(struct ext2_vol *v,
				   struct ext2_inode *inode, uint32_t index,
				   uint32_t *block)
{
	uint32_t *table;
	uint32_t new_block;
	int ret;

	ret = ext2_inode_block(v, inode, index, block);
	if (ret != 0 || *block != 0)
		return ret;
	if (index < EXT2_NDIR_BLOCKS)
	{
		ret = ext2_allocate_block(v, &new_block);
		if (ret == 0)
		{
			inode->i_block[index] = new_block;
			inode->i_blocks += v->block_size / 512;
			*block = new_block;
		}
		return ret;
	}
	index -= EXT2_NDIR_BLOCKS;
	table = kmalloc(v->block_size);
	if (!table)
		return -ENOMEM;
	if (inode->i_block[EXT2_IND_BLOCK] == 0)
	{
		ret = ext2_allocate_block(v, &new_block);
		if (ret != 0)
		{
			kfree(table);
			return ret;
		}
		inode->i_block[EXT2_IND_BLOCK] = new_block;
		inode->i_blocks += v->block_size / 512;
		memset(table, 0, v->block_size);
	}
	else
	{
		ret = ext2_read_block(v, inode->i_block[EXT2_IND_BLOCK], table);
		if (ret != 0)
		{
			kfree(table);
			return ret;
		}
	}
	ret = ext2_allocate_block(v, &new_block);
	if (ret == 0)
	{
		table[index] = new_block;
		ret = ext2_write_block(v, inode->i_block[EXT2_IND_BLOCK], table);
		if (ret == 0)
		{
			inode->i_blocks += v->block_size / 512;
			*block = new_block;
		}
	}
	kfree(table);
	return ret;
}

static struct ext2_vol *ext2_find(const char *path, char *rel, size_t rel_sz, int *is_root)
{
	size_t best = 0;
	struct ext2_vol *best_vol = NULL;

	if (!path || !rel)
		return NULL;
	for (int i = 0; i < EXT2_MAX_MOUNTS; i++)
	{
		struct ext2_vol *v = &g_vols[i];
		size_t mlen;

		if (!v->in_use)
			continue;
		mlen = strlen(v->mount_path);
		if (strncmp(path, v->mount_path, mlen) != 0)
			continue;
		if (path[mlen] != '\0' && path[mlen] != '/')
			continue;
		if (mlen >= best)
		{
			best = mlen;
			best_vol = v;
		}
	}
	if (!best_vol)
		return NULL;
	if (path[best] == '\0' || (path[best] == '/' && path[best + 1] == '\0'))
	{
		*is_root = 1;
		rel[0] = '\0';
	}
	else
	{
		*is_root = 0;
		strncpy(rel, path + best + (path[best] == '/' ? 1 : 0), rel_sz - 1);
		rel[rel_sz - 1] = '\0';
	}
	return best_vol;
}

static int ext2_lookup(struct ext2_vol *v, uint32_t dir_ino, const char *name,
		       uint32_t *out_ino)
{
	struct ext2_inode ino;
	uint8_t *buf;
	uint32_t b;
	int ret;

	ret = ext2_read_inode(v, dir_ino, &ino);
	if (ret != 0)
		return ret;
	if ((ino.i_mode & 0xF000) != 0x4000)
		return -ENOTDIR;

	buf = kmalloc(v->block_size);
	if (!buf)
		return -ENOMEM;

	for (b = 0; b < 12; b++)
	{
		uint32_t off = 0;

		if (ino.i_block[b] == 0)
			break;
		ret = ext2_read_block(v, ino.i_block[b], buf);
		if (ret != 0)
		{
			kfree(buf);
			return ret;
		}
		while (off + 8 <= v->block_size)
		{
			struct ext2_dirent *de = (struct ext2_dirent *)(buf + off);

			if (de->rec_len < 8 || de->rec_len > v->block_size - off)
			{
				kfree(buf);
				return -EIO;
			}
			if (de->name_len > de->rec_len - 8)
			{
				kfree(buf);
				return -EIO;
			}
			if (de->inode != 0 && de->name_len == strlen(name) &&
			    memcmp((char *)de + 8, name, de->name_len) == 0)
			{
				*out_ino = de->inode;
				kfree(buf);
				return 0;
			}
			off += de->rec_len;
		}
	}
	kfree(buf);
	return -ENOENT;
}

static int ext2_resolve(struct ext2_vol *v, const char *rel, uint32_t *out_ino)
{
	char path[EXT2_MAX_PATH];
	char *component;
	uint32_t current = EXT2_ROOT_INO;
	int ret;

	if (!rel || rel[0] == '\0')
	{
		*out_ino = EXT2_ROOT_INO;
		return 0;
	}
	if (strlen(rel) >= sizeof(path))
		return -ENAMETOOLONG;
	strcpy(path, rel);
	component = path;
	while (*component)
	{
		char *slash = strchr(component, '/');

		if (slash)
			*slash = '\0';
		if (*component != '\0')
		{
			ret = ext2_lookup(v, current, component, &current);
			if (ret != 0)
				return ret;
		}
		if (!slash)
			break;
		component = slash + 1;
	}
	*out_ino = current;
	return 0;
}

static int ext2_resolve_parent(struct ext2_vol *v, const char *rel,
			       uint32_t *parent, char *name, size_t name_size)
{
	char path[EXT2_MAX_PATH];
	char *slash;

	if (!rel || rel[0] == '\0' || strlen(rel) >= sizeof(path))
		return -EINVAL;
	strcpy(path, rel);
	while (path[0] && path[strlen(path) - 1] == '/')
		path[strlen(path) - 1] = '\0';
	slash = strrchr(path, '/');
	if (slash)
	{
		*slash = '\0';
		if (ext2_resolve(v, path, parent) != 0)
			return -ENOENT;
		slash++;
	}
	else
	{
		*parent = EXT2_ROOT_INO;
		slash = path;
	}
	if (slash[0] == '\0' || strlen(slash) > EXT2_NAME_MAX ||
	    strlen(slash) >= name_size)
		return -ENAMETOOLONG;
	strcpy(name, slash);
	return 0;
}

static int ext2_stat_path(const char *path, stat_t *st)
{
	char rel[EXT2_MAX_PATH];
	int is_root = 0;
	struct ext2_vol *v;
	struct ext2_inode ino;
	uint32_t inum;
	int ret;

	v = ext2_find(path, rel, sizeof(rel), &is_root);
	if (!v)
		return -ENOENT;
	ret = ext2_resolve(v, is_root ? "" : rel, &inum);
	if (ret != 0)
		return ret;
	ret = ext2_read_inode(v, inum, &ino);
	if (ret != 0)
		return ret;
	memset(st, 0, sizeof(*st));
	st->st_mode = ino.i_mode;
	st->st_uid = ino.i_uid;
	st->st_gid = ino.i_gid;
	st->st_size = ino.i_size;
	st->st_nlink = ino.i_links_count;
	st->st_ino = inum;
	st->st_blksize = v->block_size;
	st->st_blocks = ino.i_blocks;
	st->st_atime = ino.i_atime;
	st->st_mtime = ino.i_mtime;
	st->st_ctime = ino.i_ctime;
	return 0;
}

static int ext2_read_path(const char *path, void *buf, size_t count, size_t *nread,
			  off_t offset)
{
	char rel[EXT2_MAX_PATH];
	int is_root = 0;
	struct ext2_vol *v;
	struct ext2_inode ino;
	uint32_t inum;
	uint8_t *block_buf;
	size_t done = 0;
	int ret;

	if (offset < 0)
		return -EINVAL;
	v = ext2_find(path, rel, sizeof(rel), &is_root);
	if (!v)
		return -ENOENT;
	ret = ext2_resolve(v, is_root ? "" : rel, &inum);
	if (ret != 0)
		return ret;
	ret = ext2_read_inode(v, inum, &ino);
	if (ret != 0)
		return ret;
	if ((ino.i_mode & 0xF000) == 0x4000)
		return -EISDIR;
	if ((uint64_t)offset >= ino.i_size)
	{
		*nread = 0;
		return 0;
	}
	if ((uint64_t)offset + count > ino.i_size)
		count = (size_t)(ino.i_size - (uint32_t)offset);

	block_buf = kmalloc(v->block_size);
	if (!block_buf)
		return -ENOMEM;

	while (done < count)
	{
		uint32_t file_off = (uint32_t)offset + (uint32_t)done;
		uint32_t blk_index = file_off / v->block_size;
		uint32_t blk_off = file_off % v->block_size;
		uint32_t phys;
		size_t chunk;

		if (blk_index >= ext2_max_file_blocks(v))
		{
			ret = -EFBIG; /* no indirect blocks in MVP */
			break;
		}
		chunk = v->block_size - blk_off;
		if (chunk > count - done)
			chunk = count - done;
		ret = ext2_inode_block(v, &ino, blk_index, &phys);
		if (ret != 0)
			break;
		if (phys == 0)
			memset((uint8_t *)buf + done, 0, chunk);
		else
		{
			ret = ext2_read_block(v, phys, block_buf);
			if (ret != 0)
				break;
			memcpy((uint8_t *)buf + done, block_buf + blk_off, chunk);
		}
		done += chunk;
	}
	kfree(block_buf);
	if (ret != 0 && done == 0)
		return ret;
	*nread = done;
	return 0;
}

static int ext2_readdir_path(const char *path, struct vfs_dirent *entries, int max)
{
	char rel[EXT2_MAX_PATH];
	int is_root = 0;
	struct ext2_vol *v;
	struct ext2_inode ino;
	uint32_t inum;
	uint8_t *buf;
	int count = 0;
	int ret;
	uint32_t b;

	v = ext2_find(path, rel, sizeof(rel), &is_root);
	if (!v)
		return -ENOENT;
	ret = ext2_resolve(v, is_root ? "" : rel, &inum);
	if (ret != 0)
		return ret;
	ret = ext2_read_inode(v, inum, &ino);
	if (ret != 0)
		return ret;
	if ((ino.i_mode & 0xF000) != 0x4000)
		return -ENOTDIR;

	buf = kmalloc(v->block_size);
	if (!buf)
		return -ENOMEM;

	for (b = 0; b < 12 && count < max; b++)
	{
		uint32_t off = 0;

		if (ino.i_block[b] == 0)
			break;
		ret = ext2_read_block(v, ino.i_block[b], buf);
		if (ret != 0)
		{
			kfree(buf);
			return ret;
		}
		while (off + 8 <= v->block_size && count < max)
		{
			struct ext2_dirent *de = (struct ext2_dirent *)(buf + off);

			if (de->rec_len < 8 || de->rec_len > v->block_size - off ||
			    de->name_len > de->rec_len - 8)
			{
				kfree(buf);
				return -EIO;
			}
			if (de->inode != 0 && de->name_len > 0)
			{
				size_t n = de->name_len;

				if (n >= VFS_PATH_MAX)
					n = VFS_PATH_MAX - 1;
				memcpy(entries[count].name, (char *)de + 8, n);
				entries[count].name[n] = '\0';
				entries[count].type =
					(de->file_type == 2) ? DT_DIR : DT_REG;
				count++;
			}
			off += de->rec_len;
		}
	}
	kfree(buf);
	return count;
}

static uint16_t ext2_dir_rec_len(size_t name_len)
{
	return (uint16_t)((8 + name_len + 3) & ~3U);
}

static int ext2_bitmap_allocate(struct ext2_vol *v, uint32_t bitmap_block,
				uint32_t first, uint32_t count, uint32_t *number)
{
	uint8_t *bitmap;
	uint32_t bit;
	int ret;

	bitmap = kmalloc(v->block_size);
	if (!bitmap)
		return -ENOMEM;
	ret = ext2_read_block(v, bitmap_block, bitmap);
	if (ret != 0)
	{
		kfree(bitmap);
		return ret;
	}
	for (bit = first; bit < count; bit++)
	{
		if ((bitmap[bit / 8] & (1U << (bit % 8))) == 0)
		{
			bitmap[bit / 8] |= (uint8_t)(1U << (bit % 8));
			ret = ext2_write_block(v, bitmap_block, bitmap);
			if (ret == 0)
				*number = bit + 1;
			kfree(bitmap);
			return ret;
		}
	}
	kfree(bitmap);
	return -ENOSPC;
}

static int ext2_bitmap_free(struct ext2_vol *v, uint32_t bitmap_block,
			    uint32_t number)
{
	uint8_t *bitmap;
	uint32_t bit;
	int ret;

	if (number == 0)
		return -EINVAL;
	bit = number - 1;
	bitmap = kmalloc(v->block_size);
	if (!bitmap)
		return -ENOMEM;
	ret = ext2_read_block(v, bitmap_block, bitmap);
	if (ret == 0)
	{
		bitmap[bit / 8] &= (uint8_t)~(1U << (bit % 8));
		ret = ext2_write_block(v, bitmap_block, bitmap);
	}
	kfree(bitmap);
	return ret;
}

static int ext2_allocate_inode(struct ext2_vol *v, uint32_t *ino)
{
	uint32_t first = v->super.s_first_ino ? v->super.s_first_ino - 1 : 10;
	int ret = ext2_bitmap_allocate(v, v->inode_bitmap_block, first,
				       v->inodes_count, ino);

	if (ret != 0)
		return ret;
	v->super.s_free_inodes_count--;
	v->bgd.bg_free_inodes_count--;
	ret = ext2_write_metadata(v);
	if (ret != 0)
		(void)ext2_bitmap_free(v, v->inode_bitmap_block, *ino);
	return ret;
}

static int ext2_allocate_block(struct ext2_vol *v, uint32_t *block)
{
	uint32_t relative;
	int ret = ext2_bitmap_allocate(v, v->block_bitmap_block, 0,
				       v->blocks_count - v->first_data_block,
				       &relative);

	if (ret != 0)
		return ret;
	*block = v->first_data_block + relative - 1;
	v->super.s_free_blocks_count--;
	v->bgd.bg_free_blocks_count--;
	ret = ext2_write_metadata(v);
	if (ret != 0)
		(void)ext2_bitmap_free(v, v->block_bitmap_block, relative);
	return ret;
}

static int ext2_free_block(struct ext2_vol *v, uint32_t block)
{
	int ret;

	if (block < v->first_data_block)
		return -EINVAL;
	ret = ext2_bitmap_free(v, v->block_bitmap_block,
			       block - v->first_data_block + 1);
	if (ret != 0)
		return ret;
	v->super.s_free_blocks_count++;
	v->bgd.bg_free_blocks_count++;
	return ext2_write_metadata(v);
}

static void ext2_free_all_inode_blocks(struct ext2_vol *v,
				       struct ext2_inode *inode)
{
	uint32_t i;

	for (i = 0; i < EXT2_NDIR_BLOCKS; i++)
		if (inode->i_block[i])
			(void)ext2_free_block(v, inode->i_block[i]);
	if (inode->i_block[EXT2_IND_BLOCK])
	{
		uint32_t *table = kmalloc(v->block_size);

		if (table && ext2_read_block(v, inode->i_block[EXT2_IND_BLOCK],
					     table) == 0)
		{
			for (i = 0; i < v->block_size / sizeof(uint32_t); i++)
				if (table[i])
					(void)ext2_free_block(v, table[i]);
		}
		if (table)
			kfree(table);
		(void)ext2_free_block(v, inode->i_block[EXT2_IND_BLOCK]);
	}
}

static int ext2_dir_add(struct ext2_vol *v, uint32_t dir_ino,
			const char *name, uint32_t child, uint8_t type)
{
	struct ext2_inode dir;
	uint8_t *buf;
	uint16_t needed = ext2_dir_rec_len(strlen(name));
	uint32_t index;
	int ret;

	ret = ext2_read_inode(v, dir_ino, &dir);
	if (ret != 0)
		return ret;
	buf = kmalloc(v->block_size);
	if (!buf)
		return -ENOMEM;
	for (index = 0; index < EXT2_NDIR_BLOCKS; index++)
	{
		uint32_t off = 0;

		if (dir.i_block[index] == 0)
		{
			uint32_t new_block;

			ret = ext2_allocate_block(v, &new_block);
			if (ret != 0)
				break;
			dir.i_block[index] = new_block;
			memset(buf, 0, v->block_size);
			((struct ext2_dirent *)buf)->rec_len = v->block_size;
			dir.i_size += v->block_size;
			dir.i_blocks += v->block_size / 512;
		}
		else
		{
			ret = ext2_read_block(v, dir.i_block[index], buf);
			if (ret != 0)
				break;
		}
		while (off + 8 <= v->block_size)
		{
			struct ext2_dirent *de = (struct ext2_dirent *)(buf + off);
			uint16_t used;

			if (de->rec_len < 8 || de->rec_len > v->block_size - off)
			{
				ret = -EIO;
				break;
			}
			used = de->inode ? ext2_dir_rec_len(de->name_len) : 0;
			if (de->rec_len - used >= needed)
			{
				struct ext2_dirent *new_de;
				uint16_t available = de->rec_len - used;

				if (used)
					de->rec_len = used;
				new_de = (struct ext2_dirent *)(buf + off + used);
				new_de->inode = child;
				new_de->rec_len = available;
				new_de->name_len = (uint8_t)strlen(name);
				new_de->file_type = type;
				memcpy((char *)new_de + 8, name, new_de->name_len);
				ret = ext2_write_block(v, dir.i_block[index], buf);
				if (ret == 0)
					ret = ext2_write_inode(v, dir_ino, &dir);
				kfree(buf);
				return ret;
			}
			off += de->rec_len;
		}
		if (ret != 0)
			break;
	}
	kfree(buf);
	return ret == 0 ? -ENOSPC : ret;
}

static int ext2_dir_remove(struct ext2_vol *v, uint32_t dir_ino,
			   const char *name, uint32_t *removed)
{
	struct ext2_inode dir;
	uint8_t *buf;
	uint32_t index;
	int ret;

	ret = ext2_read_inode(v, dir_ino, &dir);
	if (ret != 0)
		return ret;
	buf = kmalloc(v->block_size);
	if (!buf)
		return -ENOMEM;
	for (index = 0; index < EXT2_NDIR_BLOCKS && dir.i_block[index]; index++)
	{
		uint32_t off = 0;
		struct ext2_dirent *previous = NULL;

		ret = ext2_read_block(v, dir.i_block[index], buf);
		if (ret != 0)
			break;
		while (off + 8 <= v->block_size)
		{
			struct ext2_dirent *de = (struct ext2_dirent *)(buf + off);

			if (de->rec_len < 8 || de->rec_len > v->block_size - off)
			{
				ret = -EIO;
				break;
			}
			if (de->inode && de->name_len == strlen(name) &&
			    memcmp((char *)de + 8, name, de->name_len) == 0)
			{
				*removed = de->inode;
				if (previous)
					previous->rec_len += de->rec_len;
				else
					de->inode = 0;
				ret = ext2_write_block(v, dir.i_block[index], buf);
				kfree(buf);
				return ret;
			}
			previous = de;
			off += de->rec_len;
		}
		if (ret != 0)
			break;
	}
	kfree(buf);
	return ret == 0 ? -ENOENT : ret;
}

static int ext2_create_common(const char *path, mode_t mode, int directory)
{
	char rel[EXT2_MAX_PATH];
	char name[EXT2_NAME_MAX + 1];
	int is_root = 0;
	struct ext2_vol *v = ext2_find(path, rel, sizeof(rel), &is_root);
	struct ext2_inode inode;
	const struct ir0_task_cred *cred;
	uint32_t parent;
	uint32_t ino;
	uint32_t existing;
	int ret;

	if (!v || is_root)
		return is_root ? -EEXIST : -ENOENT;
	ret = ext2_resolve_parent(v, rel, &parent, name, sizeof(name));
	if (ret != 0)
		return ret;
	if (ext2_lookup(v, parent, name, &existing) == 0)
		return -EEXIST;
	ret = ext2_allocate_inode(v, &ino);
	if (ret != 0)
		return ret;
	memset(&inode, 0, sizeof(inode));
	cred = ir0_current_cred();
	inode.i_mode = (uint16_t)((directory ? EXT2_S_IFDIR : EXT2_S_IFREG) |
				 (mode & ~(cred ? cred->umask : 0) & 07777));
	inode.i_uid = cred ? (uint16_t)cred->euid : 0;
	inode.i_gid = cred ? (uint16_t)cred->egid : 0;
	inode.i_atime = (uint32_t)clock_get_current_time();
	inode.i_ctime = inode.i_atime;
	inode.i_mtime = inode.i_atime;
	inode.i_links_count = directory ? 2 : 1;
	if (directory)
	{
		uint8_t *block;
		uint32_t new_block;
		struct ext2_dirent *dot;
		struct ext2_dirent *dotdot;

		ret = ext2_allocate_block(v, &new_block);
		if (ret != 0)
			goto fail_inode;
		inode.i_block[0] = new_block;
		inode.i_size = v->block_size;
		inode.i_blocks = v->block_size / 512;
		block = kmalloc(v->block_size);
		if (!block)
		{
			ret = -ENOMEM;
			goto fail_block;
		}
		memset(block, 0, v->block_size);
		dot = (struct ext2_dirent *)block;
		dot->inode = ino;
		dot->rec_len = ext2_dir_rec_len(1);
		dot->name_len = 1;
		dot->file_type = EXT2_FT_DIR;
		memcpy((char *)dot + 8, ".", 1);
		dotdot = (struct ext2_dirent *)(block + dot->rec_len);
		dotdot->inode = parent;
		dotdot->rec_len = v->block_size - dot->rec_len;
		dotdot->name_len = 2;
		dotdot->file_type = EXT2_FT_DIR;
		memcpy((char *)dotdot + 8, "..", 2);
		ret = ext2_write_block(v, inode.i_block[0], block);
		kfree(block);
		if (ret != 0)
			goto fail_block;
	}
	ret = ext2_write_inode(v, ino, &inode);
	if (ret != 0)
		goto fail_block;
	ret = ext2_dir_add(v, parent, name, ino,
			   directory ? EXT2_FT_DIR : EXT2_FT_REG_FILE);
	if (ret != 0)
		goto fail_block;
	if (directory)
	{
		struct ext2_inode parent_inode;

		if (ext2_read_inode(v, parent, &parent_inode) == 0)
		{
			parent_inode.i_links_count++;
			(void)ext2_write_inode(v, parent, &parent_inode);
		}
		v->bgd.bg_used_dirs_count++;
		(void)ext2_write_metadata(v);
	}
	return 0;

fail_block:
	if (inode.i_block[0])
		(void)ext2_free_block(v, inode.i_block[0]);
fail_inode:
	(void)ext2_bitmap_free(v, v->inode_bitmap_block, ino);
	v->super.s_free_inodes_count++;
	v->bgd.bg_free_inodes_count++;
	(void)ext2_write_metadata(v);
	return ret;
}

static int ext2_mkdir_path(const char *path, mode_t mode)
{
	return ext2_create_common(path, mode, 1);
}

static int ext2_create_path(const char *path, mode_t mode)
{
	return ext2_create_common(path, mode, 0);
}

static int ext2_change_links(struct ext2_vol *v, uint32_t ino, int delta)
{
	struct ext2_inode inode;
	int ret = ext2_read_inode(v, ino, &inode);

	if (ret != 0)
		return ret;
	if (delta > 0 && inode.i_links_count == 0xFFFF)
		return -EMLINK;
	if (delta < 0 && inode.i_links_count == 0)
		return -EIO;
	inode.i_links_count = (uint16_t)(inode.i_links_count + delta);
	return ext2_write_inode(v, ino, &inode);
}

static int ext2_unlink_path(const char *path)
{
	char rel[EXT2_MAX_PATH];
	char name[EXT2_NAME_MAX + 1];
	int is_root = 0;
	struct ext2_vol *v = ext2_find(path, rel, sizeof(rel), &is_root);
	struct ext2_inode inode;
	uint32_t parent;
	uint32_t ino;
	int ret;

	if (!v || is_root)
		return -ENOENT;
	ret = ext2_resolve_parent(v, rel, &parent, name, sizeof(name));
	if (ret != 0)
		return ret;
	ret = ext2_lookup(v, parent, name, &ino);
	if (ret != 0)
		return ret;
	ret = ext2_read_inode(v, ino, &inode);
	if (ret != 0)
		return ret;
	if ((inode.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR)
		return -EISDIR;
	ret = ext2_dir_remove(v, parent, name, &ino);
	if (ret != 0)
		return ret;
	ret = ext2_change_links(v, ino, -1);
	if (ret != 0)
		return ret;
	if (inode.i_links_count == 1)
	{
		ext2_free_all_inode_blocks(v, &inode);
		memset(&inode, 0, sizeof(inode));
		(void)ext2_write_inode(v, ino, &inode);
		(void)ext2_bitmap_free(v, v->inode_bitmap_block, ino);
		v->super.s_free_inodes_count++;
		v->bgd.bg_free_inodes_count++;
		(void)ext2_write_metadata(v);
	}
	return 0;
}

static int ext2_rmdir_path(const char *path)
{
	char rel[EXT2_MAX_PATH];
	char name[EXT2_NAME_MAX + 1];
	int is_root = 0;
	struct ext2_vol *v = ext2_find(path, rel, sizeof(rel), &is_root);
	struct ext2_inode inode;
	struct ext2_inode parent_inode;
	uint8_t *buf;
	uint32_t parent;
	uint32_t ino;
	uint32_t i;
	int ret;

	if (!v || is_root)
		return -EBUSY;
	ret = ext2_resolve_parent(v, rel, &parent, name, sizeof(name));
	if (ret != 0)
		return ret;
	ret = ext2_lookup(v, parent, name, &ino);
	if (ret != 0)
		return ret;
	ret = ext2_read_inode(v, ino, &inode);
	if (ret != 0)
		return ret;
	if ((inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
		return -ENOTDIR;
	buf = kmalloc(v->block_size);
	if (!buf)
		return -ENOMEM;
	for (i = 0; i < EXT2_NDIR_BLOCKS && inode.i_block[i]; i++)
	{
		uint32_t off = 0;

		ret = ext2_read_block(v, inode.i_block[i], buf);
		if (ret != 0)
			break;
		while (off + 8 <= v->block_size)
		{
			struct ext2_dirent *de = (struct ext2_dirent *)(buf + off);

			if (de->rec_len < 8 || de->rec_len > v->block_size - off)
			{
				ret = -EIO;
				break;
			}
			if (de->inode && !(de->name_len == 1 &&
			    memcmp((char *)de + 8, ".", 1) == 0) &&
			    !(de->name_len == 2 &&
			    memcmp((char *)de + 8, "..", 2) == 0))
			{
				kfree(buf);
				return -ENOTEMPTY;
			}
			off += de->rec_len;
		}
		if (ret != 0)
			break;
	}
	kfree(buf);
	if (ret != 0)
		return ret;
	ret = ext2_dir_remove(v, parent, name, &ino);
	if (ret != 0)
		return ret;
	for (i = 0; i < EXT2_NDIR_BLOCKS; i++)
		if (inode.i_block[i])
			(void)ext2_free_block(v, inode.i_block[i]);
	memset(&inode, 0, sizeof(inode));
	(void)ext2_write_inode(v, ino, &inode);
	(void)ext2_bitmap_free(v, v->inode_bitmap_block, ino);
	v->super.s_free_inodes_count++;
	v->bgd.bg_free_inodes_count++;
	if (v->bgd.bg_used_dirs_count)
		v->bgd.bg_used_dirs_count--;
	if (ext2_read_inode(v, parent, &parent_inode) == 0 &&
	    parent_inode.i_links_count)
	{
		parent_inode.i_links_count--;
		(void)ext2_write_inode(v, parent, &parent_inode);
	}
	return ext2_write_metadata(v);
}

static int ext2_link_path(const char *oldpath, const char *newpath)
{
	char old_rel[EXT2_MAX_PATH];
	char new_rel[EXT2_MAX_PATH];
	char name[EXT2_NAME_MAX + 1];
	int old_root = 0;
	int new_root = 0;
	struct ext2_vol *v = ext2_find(oldpath, old_rel, sizeof(old_rel), &old_root);
	struct ext2_vol *new_v;
	struct ext2_inode inode;
	uint32_t ino;
	uint32_t parent;
	uint32_t existing;
	int ret;

	if (!v || old_root)
		return -ENOENT;
	new_v = ext2_find(newpath, new_rel, sizeof(new_rel), &new_root);
	if (new_v != v)
		return -EXDEV;
	if (new_root)
		return -EEXIST;
	ret = ext2_resolve(v, old_rel, &ino);
	if (ret != 0)
		return ret;
	ret = ext2_read_inode(v, ino, &inode);
	if (ret != 0)
		return ret;
	if ((inode.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR)
		return -EPERM;
	ret = ext2_resolve_parent(v, new_rel, &parent, name, sizeof(name));
	if (ret != 0)
		return ret;
	if (ext2_lookup(v, parent, name, &existing) == 0)
		return -EEXIST;
	ret = ext2_dir_add(v, parent, name, ino, EXT2_FT_REG_FILE);
	if (ret != 0)
		return ret;
	ret = ext2_change_links(v, ino, 1);
	if (ret != 0)
	{
		(void)ext2_dir_remove(v, parent, name, &existing);
		return ret;
	}
	return 0;
}

static int ext2_chown_path(const char *path, uid_t owner, gid_t group)
{
	char rel[EXT2_MAX_PATH];
	int is_root = 0;
	struct ext2_vol *v = ext2_find(path, rel, sizeof(rel), &is_root);
	struct ext2_inode inode;
	uint32_t ino;
	int ret;

	if (!v)
		return -ENOENT;
	ret = ext2_resolve(v, is_root ? "" : rel, &ino);
	if (ret != 0)
		return ret;
	ret = ext2_read_inode(v, ino, &inode);
	if (ret == 0)
	{
		inode.i_uid = (uint16_t)owner;
		inode.i_gid = (uint16_t)group;
		ret = ext2_write_inode(v, ino, &inode);
	}
	return ret;
}

static int ext2_chmod_path(const char *path, mode_t mode)
{
	char rel[EXT2_MAX_PATH];
	int is_root = 0;
	struct ext2_vol *v = ext2_find(path, rel, sizeof(rel), &is_root);
	struct ext2_inode inode;
	uint32_t ino;
	int ret;

	if (!v)
		return -ENOENT;
	ret = ext2_resolve(v, is_root ? "" : rel, &ino);
	if (ret != 0)
		return ret;
	ret = ext2_read_inode(v, ino, &inode);
	if (ret == 0)
	{
		inode.i_mode = (uint16_t)((inode.i_mode & EXT2_S_IFMT) |
					 (mode & 07777));
		ret = ext2_write_inode(v, ino, &inode);
	}
	return ret;
}

static int ext2_write_path(const char *path, const void *buf, size_t count,
			   size_t *bytes_written, off_t offset)
{
	char rel[EXT2_MAX_PATH];
	int is_root = 0;
	struct ext2_vol *v;
	struct ext2_inode inode;
	uint8_t *block_buf;
	uint32_t ino;
	size_t done = 0;
	int ret;

	if (offset < 0 || !bytes_written)
		return -EINVAL;
	*bytes_written = 0;
	v = ext2_find(path, rel, sizeof(rel), &is_root);
	if (!v)
		return -ENOENT;
	ret = ext2_resolve(v, is_root ? "" : rel, &ino);
	if (ret != 0)
		return ret;
	ret = ext2_read_inode(v, ino, &inode);
	if (ret != 0)
		return ret;
	if ((inode.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR)
		return -EISDIR;
	if ((uint64_t)offset + count >
	    (uint64_t)ext2_max_file_blocks(v) * v->block_size)
		return -EFBIG;
	block_buf = kmalloc(v->block_size);
	if (!block_buf)
		return -ENOMEM;
	ret = 0;
	while (done < count)
	{
		uint32_t file_off = (uint32_t)offset + (uint32_t)done;
		uint32_t index = file_off / v->block_size;
		uint32_t block_off = file_off % v->block_size;
		uint32_t new_block;
		size_t chunk = v->block_size - block_off;

		if (chunk > count - done)
			chunk = count - done;
		ret = ext2_inode_block(v, &inode, index, &new_block);
		if (ret != 0)
			break;
		if (new_block == 0)
		{
			ret = ext2_inode_ensure_block(v, &inode, index, &new_block);
			if (ret != 0)
				break;
			memset(block_buf, 0, v->block_size);
		}
		else
		{
			ret = ext2_read_block(v, new_block, block_buf);
			if (ret != 0)
				break;
		}
		memcpy(block_buf + block_off, (const uint8_t *)buf + done, chunk);
		ret = ext2_write_block(v, new_block, block_buf);
		if (ret != 0)
			break;
		done += chunk;
	}
	if ((uint64_t)offset + done > inode.i_size)
		inode.i_size = (uint32_t)((uint64_t)offset + done);
	if (done != 0)
	{
		int inode_ret = ext2_write_inode(v, ino, &inode);

		if (ret == 0)
			ret = inode_ret;
	}
	kfree(block_buf);
	*bytes_written = done;
	return done ? 0 : ret;
}

static int ext2_truncate_path(const char *path, size_t length)
{
	char rel[EXT2_MAX_PATH];
	int is_root = 0;
	struct ext2_vol *v = ext2_find(path, rel, sizeof(rel), &is_root);
	struct ext2_inode inode;
	uint32_t ino;
	uint32_t keep;
	uint32_t i;
	int ret;

	if (!v)
		return -ENOENT;
	if (length > ext2_max_file_blocks(v) * v->block_size)
		return -EFBIG;
	ret = ext2_resolve(v, is_root ? "" : rel, &ino);
	if (ret != 0)
		return ret;
	ret = ext2_read_inode(v, ino, &inode);
	if (ret != 0)
		return ret;
	if ((inode.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR)
		return -EISDIR;
	if (length > inode.i_size)
		return -ENOSYS;
	keep = (length + v->block_size - 1) / v->block_size;
	for (i = keep; i < EXT2_NDIR_BLOCKS; i++)
	{
		if (inode.i_block[i])
		{
			ret = ext2_free_block(v, inode.i_block[i]);
			if (ret != 0)
				return ret;
			inode.i_block[i] = 0;
			inode.i_blocks -= v->block_size / 512;
		}
	}
	if (inode.i_block[EXT2_IND_BLOCK])
	{
		uint32_t *table = kmalloc(v->block_size);
		uint32_t first = keep > EXT2_NDIR_BLOCKS ?
			keep - EXT2_NDIR_BLOCKS : 0;
		int any = 0;

		if (!table)
			return -ENOMEM;
		ret = ext2_read_block(v, inode.i_block[EXT2_IND_BLOCK], table);
		if (ret != 0)
		{
			kfree(table);
			return ret;
		}
		for (i = first; i < v->block_size / sizeof(uint32_t); i++)
		{
			if (table[i])
			{
				ret = ext2_free_block(v, table[i]);
				if (ret != 0)
				{
					kfree(table);
					return ret;
				}
				table[i] = 0;
				inode.i_blocks -= v->block_size / 512;
			}
		}
		for (i = 0; i < v->block_size / sizeof(uint32_t); i++)
			if (table[i])
				any = 1;
		if (any)
			ret = ext2_write_block(v, inode.i_block[EXT2_IND_BLOCK], table);
		else
		{
			ret = ext2_free_block(v, inode.i_block[EXT2_IND_BLOCK]);
			inode.i_block[EXT2_IND_BLOCK] = 0;
			inode.i_blocks -= v->block_size / 512;
		}
		kfree(table);
		if (ret != 0)
			return ret;
	}
	inode.i_size = length;
	return ext2_write_inode(v, ino, &inode);
}

static struct vfs_ops ext2_ops = {
	.stat = ext2_stat_path,
	.mkdir = ext2_mkdir_path,
	.create = ext2_create_path,
	.unlink = ext2_unlink_path,
	.rmdir = ext2_rmdir_path,
	.link = ext2_link_path,
	.chown = ext2_chown_path,
	.chmod = ext2_chmod_path,
	.readdir = ext2_readdir_path,
	.read = ext2_read_path,
	.write = ext2_write_path,
	.truncate = ext2_truncate_path,
};

static int ext2_parse_dev(const char *dev, char *blk, size_t blk_sz)
{
	if (!dev || strlen(dev) < 8 || strncmp(dev, "/dev/", 5) != 0)
		return -EINVAL;
	strncpy(blk, dev + 5, blk_sz - 1);
	blk[blk_sz - 1] = '\0';
	return 0;
}

int ext2_disk_mount(const char *dev, const char *mount_dir)
{
	struct ext2_vol *slot = NULL;
	char blk[16];
	uint8_t sector[1024];
	struct ext2_super *sb;
	struct ext2_bgd *bgd;
	int ret;

	if (!dev || !mount_dir)
		return -EINVAL;
	for (int i = 0; i < EXT2_MAX_MOUNTS; i++)
	{
		if (!g_vols[i].in_use)
		{
			slot = &g_vols[i];
			break;
		}
	}
	if (!slot)
		return -ENOSPC;

	ret = ext2_parse_dev(dev, blk, sizeof(blk));
	if (ret != 0)
		return ret;
	if (!ir0_block_name_is_present(blk))
		return -ENXIO;

	/* Superblock at byte 1024 → LBA 2 for 512-byte sectors. */
	if (ir0_block_read_by_name(blk, 2, 2, sector))
		return -EIO;
	sb = (struct ext2_super *)sector;
	if (sb->s_magic != EXT2_MAGIC)
		return -EINVAL;
	if (sb->s_log_block_size > 2 || sb->s_blocks_per_group == 0 ||
	    sb->s_inodes_per_group == 0)
		return -ENOTSUPP;
	if (sb->s_feature_incompat & ~EXT2_SUPPORTED_INCOMPAT)
		return -ENOTSUPP;
	if ((sb->s_blocks_count - sb->s_first_data_block +
	     sb->s_blocks_per_group - 1) / sb->s_blocks_per_group != 1 ||
	    (sb->s_inodes_count + sb->s_inodes_per_group - 1) /
	    sb->s_inodes_per_group != 1)
		return -ENOTSUPP;

	memset(slot, 0, sizeof(*slot));
	strncpy(slot->blk_name, blk, sizeof(slot->blk_name) - 1);
	slot->block_size = 1024U << sb->s_log_block_size;
	slot->inodes_per_group = sb->s_inodes_per_group;
	slot->inode_size = sb->s_inode_size ? sb->s_inode_size : 128;
	slot->first_data_block = sb->s_first_data_block;
	slot->blocks_per_group = sb->s_blocks_per_group;
	slot->blocks_count = sb->s_blocks_count;
	slot->inodes_count = sb->s_inodes_count;
	memcpy(&slot->super, sb, sizeof(slot->super));
	if (slot->inode_size < sizeof(struct ext2_inode) ||
	    slot->inode_size > slot->block_size ||
	    slot->block_size % slot->inode_size != 0)
		return -ENOTSUPP;

	/* Group descriptor follows superblock block. */
	{
		uint32_t gd_block = slot->first_data_block + 1;
		uint8_t *gbuf = kmalloc(slot->block_size);

		if (!gbuf)
			return -ENOMEM;
		ret = 0;
		{
			uint32_t sectors = slot->block_size / 512;
			uint32_t lba = gd_block * sectors;

			if (ir0_block_read_by_name(blk, lba, sectors, gbuf))
				ret = -EIO;
		}
		if (ret != 0)
		{
			kfree(gbuf);
			return ret;
		}
		bgd = (struct ext2_bgd *)gbuf;
		slot->gd_block = gd_block;
		slot->block_bitmap_block = bgd->bg_block_bitmap;
		slot->inode_bitmap_block = bgd->bg_inode_bitmap;
		slot->inode_table_block = bgd->bg_inode_table;
		memcpy(&slot->bgd, bgd, sizeof(slot->bgd));
		kfree(gbuf);
	}

	strncpy(slot->mount_path, mount_dir, sizeof(slot->mount_path) - 1);
	slot->in_use = 1;
	klog_print("EXT2_MOUNT_OK\n");
	return 0;
}

static int ext2_mount_cb(const char *dev, const char *dir)
{
	return ext2_disk_mount(dev, dir);
}

static int ext2_umount_cb(const char *dir)
{
	for (int i = 0; i < EXT2_MAX_MOUNTS; i++)
	{
		if (g_vols[i].in_use && strcmp(g_vols[i].mount_path, dir) == 0)
		{
			g_vols[i].in_use = 0;
			return 0;
		}
	}
	return -EINVAL;
}

static struct vfs_fstype ext2_fstype = {
	.name = "ext2",
	.ops = &ext2_ops,
	.mount = ext2_mount_cb,
	.umount = ext2_umount_cb,
	.next = NULL,
};

int ext2_fs_register(void)
{
	return vfs_register_fs(&ext2_fstype);
}
