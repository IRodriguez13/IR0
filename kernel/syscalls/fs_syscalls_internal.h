/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fs_syscalls_internal.h
 * Description: Private includes and helpers shared by split fs syscall files.
 */

#pragma once

#include <kernel/syscalls.h>
#include <kernel/process.h>
#include "fs_syscalls.h"
#include "fs_path_syscalls.h"
#include "syscalls_glue.h"
#include <config.h>
#include <ir0/copy_user.h>
#include <ir0/console_backend.h>
#include <ir0/devfs.h>
#include <ir0/named_devnode.h>
#include <ir0/errno.h>
#include <ir0/fcntl.h>
#include <ir0/flock.h>
#include <ir0/open_flags.h>
#include <ir0/path.h>
#include <ir0/path_routed.h>
#include <ir0/stat_user.h>
#include <ir0/named_fifo.h>
#include <ir0/named_socket.h>
#include <ir0/named_symlink.h>
#include <ir0/eventfd.h>
#include <ir0/timerfd.h>
#include <ir0/posix_shm.h>
#include <ir0/fd_dispatch.h>
#include <ir0/fd_get.h>
#include <ir0/supervise_path.h>
#include <ir0/path_user.h>
#include <ir0/permissions.h>
#include <ir0/pipe.h>
#include <ir0/pipe_fd.h>
#include <ir0/sock_stream.h>
#include <ir0/signals.h>
#include <ir0/syscall.h>
#include <ir0/procfs.h>
#include "io_syscalls.h"
#include <ir0/pseudo_fs.h>
#include <ir0/heartfs.h>
#include <ir0/kmem.h>
#include <ir0/ktm/klog.h>
#include <ir0/ash_smoke.h>
#include <d1_12_read_diag.h>
#include <ir0/sysfs.h>
#include <ir0/uio.h>
#include <ir0/utimens.h>
#include <ir0/vfs.h>
#include <ir0/elf_loader.h>
#include <ir0/process.h>
#include <ir0/paging.h>
#include <stddef.h>
#include <string.h>

#define AT_REMOVEDIR 0x200

int fs_resolve_at_empty(int dirfd, char *resolved, size_t resolved_sz);
