/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: early_rootfs.h
 * Description: Staged "/", "/init", and Linux /dev char nodes (ISA supplies blob/usercopy)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <ir0/early_syscall.h>

#define IR0_EARLY_ROOTFS_AT_FDCWD (-100)

enum ir0_early_rootfs_node
{
	IR0_EARLY_ROOTFS_NONE = 0,
	IR0_EARLY_ROOTFS_ROOT,
	IR0_EARLY_ROOTFS_INIT,
	IR0_EARLY_ROOTFS_DEV,
	IR0_EARLY_ROOTFS_CONSOLE,
	IR0_EARLY_ROOTFS_NULL,
	IR0_EARLY_ROOTFS_ZERO,
	IR0_EARLY_ROOTFS_TTY,
	IR0_EARLY_ROOTFS_VDA,
	IR0_EARLY_ROOTFS_TMP,
	IR0_EARLY_ROOTFS_PIPE_R,
	IR0_EARLY_ROOTFS_PIPE_W,
	IR0_EARLY_ROOTFS_EVENTFD,
	IR0_EARLY_ROOTFS_MEMFD,
	IR0_EARLY_ROOTFS_TIMERFD,
	IR0_EARLY_ROOTFS_EPOLL,
	IR0_EARLY_ROOTFS_SOCK,
};

#define IR0_EARLY_ROOTFS_S_IFMT  0170000U
#define IR0_EARLY_ROOTFS_S_IFREG 0100000U
#define IR0_EARLY_ROOTFS_S_IFDIR 0040000U
#define IR0_EARLY_ROOTFS_S_IFCHR 0020000U
#define IR0_EARLY_ROOTFS_S_IFBLK 0060000U
#define IR0_EARLY_ROOTFS_S_IFIFO 0010000U
#define IR0_EARLY_ROOTFS_S_IFSOCK 0140000U
#define IR0_EARLY_ROOTFS_MODE_DIR \
	(IR0_EARLY_ROOTFS_S_IFDIR | 0755U)
#define IR0_EARLY_ROOTFS_MODE_REG \
	(IR0_EARLY_ROOTFS_S_IFREG | 0755U)
#define IR0_EARLY_ROOTFS_MODE_CHR \
	(IR0_EARLY_ROOTFS_S_IFCHR | 0622U)
#define IR0_EARLY_ROOTFS_MODE_BLK \
	(IR0_EARLY_ROOTFS_S_IFBLK | 0660U)
#define IR0_EARLY_ROOTFS_MODE_FIFO \
	(IR0_EARLY_ROOTFS_S_IFIFO | 0600U)
#define IR0_EARLY_ROOTFS_MODE_SOCK \
	(IR0_EARLY_ROOTFS_S_IFSOCK | 0666U)

/*
 * Linux uapi (eventfd/timerfd/epoll/memfd/socket):
 *   TFD_CLOEXEC / EPOLL_CLOEXEC / SOCK_CLOEXEC = O_CLOEXEC = 02000000
 *   TFD_NONBLOCK / SOCK_NONBLOCK = O_NONBLOCK = 00004000
 *   TFD_TIMER_ABSTIME = 1
 *   MFD_CLOEXEC = 1, MFD_ALLOW_SEALING = 2 (linux/memfd.h)
 *   EPOLL_CTL_ADD/DEL/MOD = 1/2/3
 *   AF_UNIX = 1, SOCK_STREAM = 1, SOCK_DGRAM = 2
 * AArch64 epoll_event is 16 bytes (events u32 + pad + data u64).
 * itimerspec is two timespecs (32 bytes on LP64).
 */
#define IR0_EARLY_TFD_CLOEXEC 02000000
#define IR0_EARLY_TFD_NONBLOCK 00004000
#define IR0_EARLY_TFD_TIMER_ABSTIME 1
#define IR0_EARLY_MFD_CLOEXEC 1U
#define IR0_EARLY_MFD_ALLOW_SEALING 2U
#define IR0_EARLY_MFD_HUGETLB 4U
#define IR0_EARLY_EPOLL_CLOEXEC 02000000
#define IR0_EARLY_EPOLL_CTL_ADD 1
#define IR0_EARLY_EPOLL_CTL_DEL 2
#define IR0_EARLY_EPOLL_CTL_MOD 3
#define IR0_EARLY_SOCK_CLOEXEC 02000000
#define IR0_EARLY_SOCK_NONBLOCK 00004000
#define IR0_EARLY_AF_UNIX 1
#define IR0_EARLY_SOCK_STREAM 1
#define IR0_EARLY_SOCK_DGRAM 2
#define IR0_EARLY_CLOCK_REALTIME 0UL
#define IR0_EARLY_CLOCK_MONOTONIC 1UL
#define IR0_EARLY_CLOCK_BOOTTIME 7UL
#define IR0_EARLY_ITIMERSPEC_BYTES 32U
#define IR0_EARLY_EPOLL_EVENT_BYTES 16U
/*
 * Linux devices.txt (docs.kernel.org/admin-guide/devices.html):
 *   /dev/null    char 1:3
 *   /dev/zero    char 1:5
 *   /dev/tty     char 5:0
 *   /dev/console char 5:1
 * GNU makedev(major, minor) for these 8-bit pairs is (major << 8) | minor.
 */
#define IR0_EARLY_ROOTFS_NULL_RDEV    0x103ULL
#define IR0_EARLY_ROOTFS_ZERO_RDEV    0x105ULL
#define IR0_EARLY_ROOTFS_TTY_RDEV     0x500ULL
#define IR0_EARLY_ROOTFS_CONSOLE_RDEV 0x501ULL
/*
 * Linux virtio_blk uses a dynamic major (often 253 in QEMU).
 * Staged /dev/vda reports 253:0 so fstat looks like a virtblk disk.
 */
#define IR0_EARLY_ROOTFS_VDA_RDEV     0xfd00ULL

/* Linux dirent.h d_type for getdents64(2). */
#define IR0_EARLY_DT_CHR 2
#define IR0_EARLY_DT_DIR 4
#define IR0_EARLY_DT_BLK 6
#define IR0_EARLY_DT_REG 8
#define IR0_EARLY_DT_FIFO 1

/* Linux poll.h bits used by staged ppoll/pselect6. */
#define IR0_EARLY_POLLIN  0x001
#define IR0_EARLY_POLLPRI 0x002
#define IR0_EARLY_POLLOUT 0x004
#define IR0_EARLY_POLLERR 0x008
#define IR0_EARLY_POLLHUP 0x010
#define IR0_EARLY_POLLNVAL 0x020

struct ir0_early_dirent_spec
{
	const char *name;
	uint64_t ino;
	uint8_t type;
};

static inline unsigned ir0_early_dirent64_reclen(unsigned namelen)
{
	unsigned raw = 19U + namelen + 1U;

	return (raw + 7U) & ~7U;
}

static inline unsigned ir0_early_dirent64_pack(uint8_t *dst, unsigned cap,
					      uint64_t ino, int64_t off,
					      uint8_t type, const char *name)
{
	unsigned namelen = 0;
	unsigned reclen;
	unsigned i;

	if (!dst || !name)
		return 0;
	while (name[namelen] != '\0')
		namelen++;
	reclen = ir0_early_dirent64_reclen(namelen);
	if (reclen == 0 || reclen > cap)
		return 0;
	for (i = 0; i < reclen; i++)
		dst[i] = 0;
	for (i = 0; i < 8; i++)
		dst[i] = (uint8_t)(ino >> (8U * i));
	for (i = 0; i < 8; i++)
		dst[8 + i] = (uint8_t)((uint64_t)off >> (8U * i));
	dst[16] = (uint8_t)(reclen & 0xffU);
	dst[17] = (uint8_t)(reclen >> 8);
	dst[18] = type;
	for (i = 0; i <= namelen; i++)
		dst[19 + i] = (uint8_t)name[i];
	return reclen;
}

static inline unsigned ir0_early_rootfs_dir_count(enum ir0_early_rootfs_node node)
{
	if (node == IR0_EARLY_ROOTFS_ROOT)
		return 5;
	if (node == IR0_EARLY_ROOTFS_DEV)
		return 6;
	if (node == IR0_EARLY_ROOTFS_TMP)
		return 2;
	return 0;
}

static inline struct ir0_early_dirent_spec
ir0_early_rootfs_dir_entry(enum ir0_early_rootfs_node node, unsigned idx)
{
	static const struct ir0_early_dirent_spec root[] = {
		{ ".", 1, IR0_EARLY_DT_DIR },
		{ "..", 1, IR0_EARLY_DT_DIR },
		{ "init", 2, IR0_EARLY_DT_REG },
		{ "dev", 3, IR0_EARLY_DT_DIR },
		{ "tmp", 9, IR0_EARLY_DT_DIR },
	};
	static const struct ir0_early_dirent_spec dev[] = {
		{ ".", 3, IR0_EARLY_DT_DIR },
		{ "..", 1, IR0_EARLY_DT_DIR },
		{ "console", 4, IR0_EARLY_DT_CHR },
		{ "null", 5, IR0_EARLY_DT_CHR },
		{ "zero", 6, IR0_EARLY_DT_CHR },
		{ "tty", 7, IR0_EARLY_DT_CHR },
	};
	struct ir0_early_dirent_spec empty = { 0, 0, 0 };

	static const struct ir0_early_dirent_spec tmpdir[] = {
		{ ".", 9, IR0_EARLY_DT_DIR },
		{ "..", 1, IR0_EARLY_DT_DIR },
	};

	if (node == IR0_EARLY_ROOTFS_ROOT && idx < 5)
		return root[idx];
	if (node == IR0_EARLY_ROOTFS_DEV && idx < 6)
		return dev[idx];
	if (node == IR0_EARLY_ROOTFS_TMP && idx < 2)
		return tmpdir[idx];
	return empty;
}

static inline int ir0_early_rootfs_path_eq(const char *path, const char *lit)
{
	if (!path || !lit)
		return 0;
	while (*path && *path == *lit)
	{
		path++;
		lit++;
	}
	return *path == *lit;
}

struct ir0_early_stat64
{
	uint64_t st_dev;
	uint64_t st_ino;
	uint32_t st_mode;
	uint32_t st_nlink;
	uint64_t st_uid;
	uint64_t st_gid;
	uint64_t st_rdev;
	uint64_t __pad1;
	int64_t st_size;
	int32_t st_blksize;
	int32_t __pad2;
	int64_t st_blocks;
	int64_t st_atime_sec;
	uint64_t st_atime_nsec;
	int64_t st_mtime_sec;
	uint64_t st_mtime_nsec;
	int64_t st_ctime_sec;
	uint64_t st_ctime_nsec;
	uint64_t __unused[2];
};

static inline enum ir0_early_rootfs_node ir0_early_rootfs_path_node(const char *path)
{
	if (!path)
		return IR0_EARLY_ROOTFS_NONE;
	if (ir0_early_rootfs_path_eq(path, "/"))
		return IR0_EARLY_ROOTFS_ROOT;
	if (ir0_early_rootfs_path_eq(path, "/init"))
		return IR0_EARLY_ROOTFS_INIT;
	if (ir0_early_rootfs_path_eq(path, "/dev"))
		return IR0_EARLY_ROOTFS_DEV;
	if (ir0_early_rootfs_path_eq(path, "/dev/console"))
		return IR0_EARLY_ROOTFS_CONSOLE;
	if (ir0_early_rootfs_path_eq(path, "/dev/null"))
		return IR0_EARLY_ROOTFS_NULL;
	if (ir0_early_rootfs_path_eq(path, "/dev/zero"))
		return IR0_EARLY_ROOTFS_ZERO;
	if (ir0_early_rootfs_path_eq(path, "/dev/tty"))
		return IR0_EARLY_ROOTFS_TTY;
	if (ir0_early_rootfs_path_eq(path, "/dev/vda"))
		return IR0_EARLY_ROOTFS_VDA;
	if (ir0_early_rootfs_path_eq(path, "/tmp"))
		return IR0_EARLY_ROOTFS_TMP;
	return IR0_EARLY_ROOTFS_NONE;
}

static inline void ir0_early_rootfs_fill_stat(enum ir0_early_rootfs_node node,
					      uint64_t blob_len,
					      struct ir0_early_stat64 *st)
{
	if (!st)
		return;

	st->st_dev = 1;
	st->st_uid = 0;
	st->st_gid = 0;
	st->st_rdev = 0;
	st->st_nlink = 1;
	st->st_blksize = 4096;
	st->st_atime_sec = 0;
	st->st_atime_nsec = 0;
	st->st_mtime_sec = 0;
	st->st_mtime_nsec = 0;
	st->st_ctime_sec = 0;
	st->st_ctime_nsec = 0;
	st->__pad1 = 0;
	st->__pad2 = 0;
	st->__unused[0] = 0;
	st->__unused[1] = 0;

	if (node == IR0_EARLY_ROOTFS_INIT)
	{
		st->st_ino = 2;
		st->st_mode = IR0_EARLY_ROOTFS_MODE_REG;
		st->st_size = (int64_t)blob_len;
		st->st_blocks = (int64_t)((blob_len + 511UL) / 512UL);
		return;
	}
	if (node == IR0_EARLY_ROOTFS_CONSOLE ||
	    node == IR0_EARLY_ROOTFS_NULL ||
	    node == IR0_EARLY_ROOTFS_ZERO ||
	    node == IR0_EARLY_ROOTFS_TTY)
	{
		st->st_mode = IR0_EARLY_ROOTFS_MODE_CHR;
		st->st_size = 0;
		st->st_blocks = 0;
		if (node == IR0_EARLY_ROOTFS_CONSOLE)
		{
			st->st_ino = 4;
			st->st_rdev = IR0_EARLY_ROOTFS_CONSOLE_RDEV;
			return;
		}
		if (node == IR0_EARLY_ROOTFS_NULL)
		{
			st->st_ino = 5;
			st->st_rdev = IR0_EARLY_ROOTFS_NULL_RDEV;
			return;
		}
		if (node == IR0_EARLY_ROOTFS_ZERO)
		{
			st->st_ino = 6;
			st->st_rdev = IR0_EARLY_ROOTFS_ZERO_RDEV;
			return;
		}
		st->st_ino = 7;
		st->st_rdev = IR0_EARLY_ROOTFS_TTY_RDEV;
		return;
	}
	if (node == IR0_EARLY_ROOTFS_VDA)
	{
		st->st_ino = 8;
		st->st_mode = IR0_EARLY_ROOTFS_MODE_BLK;
		st->st_rdev = IR0_EARLY_ROOTFS_VDA_RDEV;
		st->st_size = (int64_t)blob_len;
		st->st_blocks = (int64_t)((blob_len + 511UL) / 512UL);
		return;
	}
	if (node == IR0_EARLY_ROOTFS_DEV)
	{
		st->st_ino = 3;
		st->st_mode = IR0_EARLY_ROOTFS_MODE_DIR;
		st->st_size = 0;
		st->st_blocks = 0;
		return;
	}
	if (node == IR0_EARLY_ROOTFS_TMP)
	{
		st->st_ino = 9;
		st->st_mode = IR0_EARLY_ROOTFS_MODE_DIR;
		st->st_size = 0;
		st->st_blocks = 0;
		return;
	}
	if (node == IR0_EARLY_ROOTFS_EVENTFD)
	{
		st->st_ino = 10;
		st->st_mode = IR0_EARLY_ROOTFS_MODE_REG;
		st->st_size = 0;
		st->st_blocks = 0;
		return;
	}
	if (node == IR0_EARLY_ROOTFS_PIPE_R || node == IR0_EARLY_ROOTFS_PIPE_W)
	{
		st->st_ino = 11;
		st->st_mode = IR0_EARLY_ROOTFS_MODE_FIFO;
		st->st_size = 0;
		st->st_blocks = 0;
		return;
	}
	if (node == IR0_EARLY_ROOTFS_MEMFD)
	{
		st->st_ino = 12;
		st->st_mode = IR0_EARLY_ROOTFS_MODE_REG;
		st->st_size = (int64_t)blob_len;
		st->st_blocks = (int64_t)((blob_len + 511UL) / 512UL);
		return;
	}
	if (node == IR0_EARLY_ROOTFS_TIMERFD)
	{
		st->st_ino = 13;
		st->st_mode = IR0_EARLY_ROOTFS_MODE_REG;
		st->st_size = 0;
		st->st_blocks = 0;
		return;
	}
	if (node == IR0_EARLY_ROOTFS_EPOLL)
	{
		st->st_ino = 14;
		st->st_mode = IR0_EARLY_ROOTFS_MODE_REG;
		st->st_size = 0;
		st->st_blocks = 0;
		return;
	}
	if (node == IR0_EARLY_ROOTFS_SOCK)
	{
		st->st_ino = 15;
		st->st_mode = IR0_EARLY_ROOTFS_MODE_SOCK;
		st->st_size = 0;
		st->st_blocks = 0;
		return;
	}

	st->st_ino = 1;
	st->st_mode = IR0_EARLY_ROOTFS_MODE_DIR;
	st->st_size = 0;
	st->st_blocks = 0;
}

static inline int ir0_early_timerfd_clock_ok(uint64_t clock_id)
{
	return clock_id == IR0_EARLY_CLOCK_REALTIME ||
	       clock_id == IR0_EARLY_CLOCK_MONOTONIC ||
	       clock_id == IR0_EARLY_CLOCK_BOOTTIME;
}

static inline int ir0_early_timerfd_create_flags_ok(int flags)
{
	return (flags & ~(IR0_EARLY_TFD_CLOEXEC | IR0_EARLY_TFD_NONBLOCK)) == 0;
}

static inline int ir0_early_memfd_flags_ok(unsigned flags)
{
	if (flags & IR0_EARLY_MFD_HUGETLB)
		return 0;
	return (flags & ~(IR0_EARLY_MFD_CLOEXEC | IR0_EARLY_MFD_ALLOW_SEALING)) ==
	       0;
}

static inline int ir0_early_epoll_create_flags_ok(int flags)
{
	return (flags & ~IR0_EARLY_EPOLL_CLOEXEC) == 0;
}

static inline int ir0_early_socketpair_args_ok(int domain, int type, int protocol)
{
	int kind = type & 0xf;

	if (domain != IR0_EARLY_AF_UNIX)
		return 0;
	if (protocol != 0)
		return 0;
	if (kind != IR0_EARLY_SOCK_STREAM && kind != IR0_EARLY_SOCK_DGRAM)
		return 0;
	if (type & ~(0xf | IR0_EARLY_SOCK_CLOEXEC | IR0_EARLY_SOCK_NONBLOCK))
		return 0;
	return 1;
}

static inline uint64_t ir0_early_timespec_to_ticks(int64_t sec, int64_t nsec,
						   uint64_t hz)
{
	uint64_t seconds_ticks;
	uint64_t nanosecond_ticks;

	if (sec < 0 || nsec < 0 || nsec >= 1000000000LL || !hz)
		return 0;
	seconds_ticks = (uint64_t)sec * hz;
	nanosecond_ticks = ((uint64_t)nsec * hz) / 1000000000ULL;
	return seconds_ticks + nanosecond_ticks;
}

static inline uint16_t ir0_early_le16(const uint8_t *bytes)
{
	return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static inline uint32_t ir0_early_le32(const uint8_t *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
	       ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static inline uint64_t ir0_early_le64(const uint8_t *bytes)
{
	return (uint64_t)ir0_early_le32(bytes) |
	       ((uint64_t)ir0_early_le32(bytes + 4) << 32);
}

/*
 * Bytes covered by an ELF64 ET_EXEC/ET_DYN image (Ehdr + PT_LOAD file
 * ranges + optional section table). 0 if the buffer is not a little-endian
 * ELF64 or the headers do not fit in @cap.
 */
static inline uint64_t ir0_early_elf64_image_bytes(const uint8_t *hdr,
						   uint64_t cap)
{
	uint64_t phoff;
	uint64_t shoff;
	uint64_t size = 64;
	uint16_t phnum;
	uint16_t phentsize;
	uint16_t shnum;
	uint16_t shentsize;
	unsigned i;

	if (!hdr || cap < 64)
		return 0;
	if (hdr[0] != 0x7f || hdr[1] != 'E' || hdr[2] != 'L' || hdr[3] != 'F')
		return 0;
	if (hdr[4] != 2 || hdr[5] != 1)
		return 0;
	phoff = ir0_early_le64(hdr + 32);
	shoff = ir0_early_le64(hdr + 40);
	phentsize = ir0_early_le16(hdr + 54);
	phnum = ir0_early_le16(hdr + 56);
	shentsize = ir0_early_le16(hdr + 58);
	shnum = ir0_early_le16(hdr + 60);
	if (phoff && phentsize >= 40 && phnum)
	{
		uint64_t phend = phoff + (uint64_t)phnum * phentsize;

		if (phend > size)
			size = phend;
		for (i = 0; i < phnum; i++)
		{
			uint64_t poff = phoff + (uint64_t)i * phentsize;
			uint32_t type;
			uint64_t offset;
			uint64_t filesz;

			if (poff + 40 > cap)
				return 0;
			type = ir0_early_le32(hdr + poff);
			if (type != 1)
				continue;
			offset = ir0_early_le64(hdr + poff + 8);
			filesz = ir0_early_le64(hdr + poff + 32);
			if (offset + filesz > size)
				size = offset + filesz;
		}
	}
	if (shoff && shentsize && shnum)
	{
		uint64_t shend = shoff + (uint64_t)shnum * shentsize;

		if (shend > size)
			size = shend;
	}
	if (size == 0 || size > cap)
		return 0;
	return size;
}

void early_rootfs_bind(const uint8_t *init_blob, uint64_t init_len,
		       const struct early_user_services *user);
int early_rootfs_try_bind_block(const char *name, uint8_t *buffer,
				uint64_t buffer_len,
				const struct early_user_services *user);
const uint8_t *early_rootfs_init_blob(uint64_t *length);
int early_rootfs_init_from_block(void);
int early_rootfs_ready(void);
int64_t early_rootfs_block_write(int fd, uint64_t buffer, uint64_t count);
int early_rootfs_fd_is_block(int fd);
int early_rootfs_console_ready(void);
int early_rootfs_memdev_ready(void);
int early_rootfs_dents_ready(void);
int early_rootfs_vda_ready(void);
int early_rootfs_smoke_init(void);
int early_rootfs_fd_is_console(int fd);
int early_rootfs_fd_discards_write(int fd);
int early_rootfs_fd_is_pipe_write(int fd);
int64_t early_rootfs_pipe_write(int fd, uint64_t buffer, uint64_t count);
int64_t early_rootfs_pipe2(uint64_t fds, int flags);
int64_t early_rootfs_eventfd2(uint64_t initval, int flags);
int64_t early_rootfs_eventfd_write(int fd, uint64_t buffer, uint64_t count);
int early_rootfs_fd_is_eventfd(int fd);
void early_rootfs_note_clock(uint64_t mono_ticks, uint64_t hz,
			     int64_t realtime_off);
int64_t early_rootfs_memfd_create(uint64_t name, unsigned flags);
int64_t early_rootfs_memfd_write(int fd, uint64_t buffer, uint64_t count);
int early_rootfs_fd_is_memfd(int fd);
int64_t early_rootfs_timerfd_create(int clockid, int flags);
int64_t early_rootfs_timerfd_settime(int fd, int flags, uint64_t new_value,
				     uint64_t old_value);
int64_t early_rootfs_timerfd_gettime(int fd, uint64_t curr);
int64_t early_rootfs_epoll_create1(int flags);
int64_t early_rootfs_epoll_ctl(int epfd, int op, int fd, uint64_t event);
int64_t early_rootfs_epoll_pwait(int epfd, uint64_t events, int maxevents,
				 int timeout);
int64_t early_rootfs_socketpair(int domain, int type, int protocol, uint64_t sv);
int64_t early_rootfs_sock_write(int fd, uint64_t buffer, uint64_t count);
int early_rootfs_fd_is_sock(int fd);
int64_t early_rootfs_blkgetsize64(int fd, uint64_t dest);
int64_t early_rootfs_dup(int oldfd);
int64_t early_rootfs_dup3(int oldfd, int newfd, int flags);
int64_t early_rootfs_chdir(uint64_t path);
int64_t early_rootfs_getcwd(uint64_t buffer, uint64_t size);
int64_t early_rootfs_getdents64(int fd, uint64_t buffer, uint64_t count);
int64_t early_rootfs_lseek(int fd, int64_t offset, int whence);
int64_t early_rootfs_fcntl(int fd, int cmd, uint64_t arg);
int64_t early_rootfs_fchdir(int fd);
int64_t early_rootfs_fsync(int fd);
int64_t early_rootfs_execve(uint64_t path, uint64_t argv, uint64_t envp);
int early_rootfs_exec_pending(void);
char *const *early_rootfs_exec_argv(void);
void early_rootfs_exec_clear(void);

int64_t early_rootfs_openat(int dirfd, uint64_t path, int flags);
int64_t early_rootfs_read(int fd, uint64_t buffer, uint64_t count);
int64_t early_rootfs_close(int fd);
int64_t early_rootfs_faccessat(int dirfd, uint64_t path, int flags);
int64_t early_rootfs_newfstatat(int dirfd, uint64_t path, uint64_t statbuf,
				int flags);
int64_t early_rootfs_fstat(int fd, uint64_t statbuf);
int64_t early_rootfs_readlinkat(int dirfd, uint64_t path, uint64_t buffer,
				uint64_t size);
int early_rootfs_poll_revents(int fd, int events);
int64_t early_rootfs_statfs(uint64_t path, uint64_t buf);
int64_t early_rootfs_fstatfs(int fd, uint64_t buf);
int64_t early_rootfs_fchmod(int fd, uint64_t mode);
int64_t early_rootfs_fchmodat(int dirfd, uint64_t path, uint64_t mode);
int64_t early_rootfs_fchown(int fd, uint64_t uid, uint64_t gid);
int64_t early_rootfs_fchownat(int dirfd, uint64_t path, uint64_t uid,
			      uint64_t gid);
int64_t early_rootfs_chroot(uint64_t path);
int64_t early_rootfs_truncate(uint64_t path, uint64_t length);
int64_t early_rootfs_ftruncate(int fd, uint64_t length);
int64_t early_rootfs_flock(int fd, int op);
int64_t early_rootfs_utimensat(int dirfd, uint64_t path);
int64_t early_rootfs_mkdirat(int dirfd, uint64_t path);
int64_t early_rootfs_unlinkat(int dirfd, uint64_t path, int flags);
int64_t early_rootfs_renameat(int olddirfd, uint64_t oldpath, int newdirfd,
			      uint64_t newpath);
