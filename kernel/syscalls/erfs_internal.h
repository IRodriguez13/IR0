/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: erfs_internal.h
 * Description: Private state and helpers for the split early rootfs backend.
 *              Not a public ABI; only kernel/syscalls/erfs*.c (incl.
 *              erfs_landlock.c, erfs_xattr.c,
 *              erfs_mount.c, erfs_path.c, erfs_vfs_ops.c,
 *              erfs_fd.c, erfs_bind.c, erfs_cwd.c,
 *              erfs_stat.c, erfs_util.c, erfs_vfs.c, erfs_cpio.c)
 *              include it.
 */

#pragma once

#include <stdint.h>
#include <ir0/blockdev.h>
#include <ir0/cwd.h>
#include <ir0/early_cpio.h>
#include <ir0/fd_install.h>
#include <ir0/early_process.h>
#include <ir0/early_rootfs.h>
#include <ir0/vfs_file.h>
#include <ir0/vfs_dispatch.h>
#include <ir0/ramtab.h>
#include <ir0/early_anonfd.h>
#include <ir0/early_unix.h>
#include <ir0/bpath.h>
#include <ir0/stat.h>
#include <ir0/fcntl.h>
#include <ir0/types.h>
#include <ir0/statfs.h>
#include <ir0/errno.h>

#define AT_REMOVEDIR 0x200
#define LOCK_SH 1
#define LOCK_EX 2
#define LOCK_NB 4
#define LOCK_UN 8

#define ROOTFS_MAX_OPEN 16U
#define ROOTFS_FD_BASE  3
#define ROOTFS_FD_LIMIT ((int)(ROOTFS_FD_BASE + ROOTFS_MAX_OPEN))
#define LINUX_O_ACCMODE  IR0_O_ACCMODE

#define EXEC_ARGC_MAX 8
#define EXEC_ARG_LEN  32

struct rootfs_slot
{
	int used;
	enum ir0_early_rootfs_node node;
	uint64_t pos;
	int flags;
	int cloexec;
	int pipe_id;
	struct vfs_file *vf;
};

#define PIPE_MAX EARLY_PIPE_MAX
#define PIPE_BUF EARLY_PIPE_BUF
#define EFD_MAX EARLY_EFD_MAX
#define EFD_CLOEXEC 02000000
#define EFD_NONBLOCK 00004000
#define EFD_SEMAPHORE 1
#define EFD_COUNT_MAX 0xfffffffffffffffeULL
#define MEMFD_MAX EARLY_MEMFD_MAX
#define MEMFD_CAP EARLY_MEMFD_CAP
#define TIMERFD_MAX EARLY_TIMERFD_MAX
#define EPOLL_MAX EARLY_EPOLL_MAX
#define EPOLL_SLOTS EARLY_EPOLL_SLOTS
#define g_pipes g_early_pipes
#define g_eventfd g_early_eventfd
#define g_memfd g_early_memfd
#define g_timerfd g_early_timerfd
#define g_epoll g_early_epoll
#define SOCKPAIR_MAX EARLY_SOCKPAIR_MAX
#define UNIX_MAX EARLY_UNIX_MAX
#define UNIX_ID_BASE EARLY_UNIX_ID_BASE
#define g_sock g_early_sock
#define g_unix g_early_unix
#define NS_PER_SEC 1000000000ULL
#define pipe_consume early_pipe_consume
#define pipe_peek early_pipe_peek
#define pipe_deposit early_pipe_deposit

#define XA_MAX 16

struct early_xattr
{
	int used;
	enum ir0_early_rootfs_node node;
	int id;
	char name[IR0_EARLY_XATTR_NAME_MAX + 1];
	uint32_t vlen;
	uint8_t value[IR0_EARLY_XATTR_VALUE_MAX];
};

#define RULESET_MAX 2
#define LL_RULE_MAX 4

struct early_ll_rule
{
	uint64_t allowed;
	char path[RAMTAB_NAME];
};

struct early_ruleset
{
	int used;
	int refs;
	uint64_t handled;
	int nrules;
	struct early_ll_rule rules[LL_RULE_MAX];
};

extern struct rootfs_slot g_slots[ROOTFS_MAX_OPEN];
extern const uint8_t *g_blob;
extern uint64_t g_blob_len;
extern const struct early_user_services *g_user;
extern int g_ready;
extern int g_stdio_open[3];
extern enum ir0_early_rootfs_node g_stdio_node[3];
extern uint64_t g_stdio_pos[3];
extern int g_stdio_flags[3];
extern int g_stdio_cloexec[3];
extern enum ir0_early_rootfs_node g_cwd;
extern enum ir0_early_rootfs_node g_chroot;
extern const struct ir0_cwd_ops *g_cwd_ops;
extern int g_cwd_used;
extern const struct ir0_fd_install_ops *g_fd_ops;
extern int g_vfs_file_used;
extern int g_vfs_ops_used;
extern int g_fd_used;
extern const struct ir0_bpath_ops *g_bpath;
extern int g_bpath_used;
extern uint64_t g_init_vis;
extern int g_init_from_block;
extern int g_exec_pending;
extern int g_exec_owner;
extern int g_abstract_ok;
extern char g_exec_args[EXEC_ARGC_MAX][EXEC_ARG_LEN];
extern char *g_exec_argv[EXEC_ARGC_MAX + 1];
extern uint64_t g_clock_mono;
extern uint64_t g_clock_hz;
extern int64_t g_clock_realtime_off;
extern struct early_xattr g_xattr[XA_MAX];
extern struct early_ruleset g_ruleset[RULESET_MAX];
extern int g_ll_on;
extern uint64_t g_ll_handled;
extern int g_ll_n;
extern struct early_ll_rule g_ll_rules[LL_RULE_MAX];
extern int g_cpio_ready;
extern const uint8_t *g_cpio_buf;
extern uint64_t g_cpio_len;
extern int g_cpio_product_installed;
extern char g_cwd_path[RAMTAB_NAME];
extern char g_chroot_path[RAMTAB_NAME];

/* Cross-file helpers (private). */
int alloc_fd(enum ir0_early_rootfs_node node);
int early_fdtable_ok(int fd);
void apply_chroot(char *pbuf, unsigned cap);
uint64_t clock_now_ticks(int clock);
int close_fd(int fd);
void copy_kstr(char *dst, unsigned cap, const char *src);
int copy_user_cstr(uint64_t upath, char *buf, unsigned bufsz);
int erfs_cpio_find_init(const uint8_t *buf, uint64_t bytes,
			const uint8_t **init_data, uint64_t *init_len);
int erfs_cpio_install_members(const uint8_t *buf, uint64_t bytes);
int early_rootfs_cpio_bpath_fold(void);
void early_publish_root(void);
int64_t early_rootfs_block_write(int fd, uint64_t buffer, uint64_t count);
int64_t early_try_vfs_ops_read(int fd, enum ir0_early_rootfs_node node,
			       uint64_t buffer, uint64_t count);
int64_t early_rootfs_close(int fd);
int early_rootfs_console_ready(void);
int64_t early_rootfs_dup(int oldfd);
int64_t early_rootfs_fcntl_dupfd(int fd, int minfd, int cloexec);
int64_t early_rootfs_faccessat(int dirfd, uint64_t path, int flags);
int early_rootfs_fd_is_block(int fd);
int64_t early_rootfs_ftruncate(int fd, uint64_t length);
int64_t early_rootfs_memfd_write(int fd, uint64_t buffer, uint64_t count);
int64_t early_rootfs_openat(int dirfd, uint64_t path, int flags);
int early_rootfs_poll_revents(int fd, int events);
int64_t early_rootfs_read(int fd, uint64_t buffer, uint64_t count);
int fd_espipe(int fd);
int fd_is_open(int fd);
int flags_of_fd(int fd);
int follow_path(char *pbuf, unsigned cap);
int ir0_bpath_is_tmp(const char *path);
enum ir0_early_rootfs_node bpath_lookup(const char *path);
int64_t product_attach_open(int fd, const char *path, int flags);
int join_under_cwd(char *pbuf, unsigned cap);
int landlock_allow(const char *path, uint64_t need);
int landlock_open_need(const char *path, int flags);
int load_loff(uint64_t uptr, int64_t *out, int64_t fallback);
enum ir0_early_rootfs_node lookup_user_path(char *pbuf);
int node_is_dir(enum ir0_early_rootfs_node node);
enum ir0_early_rootfs_node node_of_fd(int fd);
void note_sender_creds_sock(int pair, int dest_end);
void note_sender_creds_unix(int uid);
int parent_exists(const char *path);
int path_of_fd(int fd, char *out, unsigned cap);
int pipe_id_of_fd(int fd);
struct early_pipe *pipe_of_fd(int fd);
uint64_t *pos_of_fd(int fd);
int bpath_live(void);
int64_t product_xfer(int fd, uint64_t buffer, uint64_t count, int write);
void set_cloexec_of_fd(int fd, int cloexec);
int cloexec_of_fd(int fd);
void set_flags_of_fd(int fd, int flags);
int slot_from_fd(int fd);
int sock_pair_of_fd(int fd);
int64_t splice_wait(int fd, unsigned flags);
int status_flags(int flags);
int store_loff(uint64_t uptr, int64_t value);
int strings_equal(const char *left, const char *right);
void timerfd_advance_all(void);
void timerfd_advance_one(struct early_timerfd *t);
int unix_find_bound(const char *path, int stream);
int unix_id_of_fd(int fd);
uint64_t vda_bytes(void);
int vda_present(void);
struct vfs_file *vf_of_fd(int fd);
int64_t wait_or_eagain(int fd);
