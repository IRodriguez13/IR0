/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: sysv_shm.h
 * Description: Minimal SysV shared memory (MIT-SHM prep).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define IR0_IPC_PRIVATE 0
#define IR0_IPC_CREAT   01000
#define IR0_IPC_EXCL    02000
#define IR0_IPC_RMID    0
#define IR0_IPC_STAT    2

struct ir0_shmid_ds
{
	size_t shm_segsz;
	int shm_nattch;
	int shm_perm_mode;
	/* pid of the task that created the segment. 0 if unknown. */
	int shm_cpid;
	/* pid of the task that last attached or detached. 0 if none. */
	int shm_lpid;
	/* Seconds. Wall clock when it is available, otherwise uptime. */
	time_t shm_atime;
	time_t shm_dtime;
	time_t shm_ctime;
};

struct process;

int64_t sys_shmget(int key, size_t size, int shmflg);
int64_t sys_shmat(int shmid, const void *shmaddr, int shmflg);
int64_t sys_shmdt(const void *shmaddr);
int64_t sys_shmctl(int shmid, int cmd, void *buf);

/* Drop every attach owned by this task. Fork does not copy attaches. */
void sysv_shm_detach_process(struct process *proc);
