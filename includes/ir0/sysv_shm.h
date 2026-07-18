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
};

int64_t sys_shmget(int key, size_t size, int shmflg);
int64_t sys_shmat(int shmid, const void *shmaddr, int shmflg);
int64_t sys_shmdt(const void *shmaddr);
int64_t sys_shmctl(int shmid, int cmd, void *buf);
