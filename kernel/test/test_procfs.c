/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_procfs.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Tests de procfs
 */

#include "test/ktest_harness.h"
#include "syscalls.h"
#include "process.h"
#include <kernel/syscalls/fs_syscalls.h>
#include <ir0/abi/mmap_contract.h>
#include <ir0/errno.h>
#include <ir0/fcntl.h>
#include <ir0/mman.h>
#include <ir0/named_fifo.h>
#include <ir0/path_routed.h>
#include <ir0/permissions.h>
#include <ir0/procfs.h>
#include <ir0/stat.h>
#include <string.h>

static int ktest_pid_to_str(int pid, char *buf, size_t buflen)
{
	char rev[16];
	int n = 0;
	int pos = 0;

	if (!buf || buflen == 0)
		return -1;

	if (pid == 0)
	{
		if (buflen < 2)
			return -1;
		buf[0] = '0';
		buf[1] = '\0';
		return 1;
	}

	while (pid > 0 && n < (int)sizeof(rev))
	{
		rev[n++] = (char)('0' + (pid % 10));
		pid /= 10;
	}

	if ((size_t)n + 1 > buflen)
		return -1;

	while (n > 0)
		buf[pos++] = rev[--n];
	buf[pos] = '\0';
	return pos;
}

void ktest_procfs_uptime(void)
{
	KTEST_BEGIN("procfs_uptime");
	int64_t fd = sys_open("/proc/uptime", 0, 0);
	KASSERT_GT(fd, 0);
	char buf[128];
	memset(buf, 0, sizeof(buf));
	int64_t n = sys_read((int)fd, buf, sizeof(buf) - 1);
	sys_close((int)fd);
	KASSERT_GE(n, 0);
	KASSERT_GT(n, 0);
	KASSERT(buf[0] >= '0' && buf[0] <= '9');
	KTEST_END();
}

void ktest_procfs_pid_status(void)
{
	pid_t pid = -1;
	const char *name;
	int64_t fd;
	char buf[256];
	int64_t n;

	KTEST_BEGIN("procfs_pid_status");
	if (!current_process)
	{
		KTEST_END();
		return;
	}

	name = proc_resolve_path("/proc/status", &pid);
	KASSERT(name != NULL);
	KASSERT(strcmp(name, "status") == 0);

	fd = sys_open("/proc/status", 0, 0);
	KASSERT_GT(fd, 0);
	KASSERT(fd < 1000);

	memset(buf, 0, sizeof(buf));
	n = sys_read((int)fd, buf, sizeof(buf) - 1);
	sys_close((int)fd);
	KASSERT_GT(n, 0);
	KASSERT(strstr(buf, "\t") != NULL);

	KTEST_END();
}

static int ktest_parse_hex64(const char **pp, uint64_t *out)
{
	const char *p = *pp;
	uint64_t value = 0;
	int digits = 0;

	for (;;)
	{
		char c = *p;
		int d;

		if (c >= '0' && c <= '9')
			d = c - '0';
		else if (c >= 'a' && c <= 'f')
			d = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')
			d = c - 'A' + 10;
		else
			break;
		if (value > (UINT64_MAX >> 4))
			return -1;
		value = (value << 4) | (uint64_t)d;
		p++;
		digits++;
	}
	if (digits == 0)
		return -1;
	*pp = p;
	*out = value;
	return 0;
}

void ktest_procfs_pid_maps(void)
{
	int64_t fd;
	char buf[2048];
	int64_t n;
	void *mapped;
	uintptr_t map_start;
	uintptr_t map_end;
	int found = 0;

	KTEST_BEGIN("procfs_pid_maps");
	if (!current_process)
	{
		KTEST_END();
		return;
	}

	/*
	 * Map one anonymous page so maps has a range whose bounds we know,
	 * then require that exact range (and its permissions) in the output.
	 * A maps file that omits tracked mmap regions must not pass.
	 */
	mapped = sys_mmap(NULL, IR0_PAGE_SIZE, PROT_READ | PROT_WRITE,
			  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	KASSERT(mapped != NULL && mapped != (void *)(intptr_t)-1);
	if (mapped == NULL || mapped == (void *)(intptr_t)-1)
	{
		KTEST_END();
		return;
	}
	map_start = (uintptr_t)mapped;
	map_end = map_start + IR0_PAGE_SIZE;

	fd = sys_open("/proc/self/maps", 0, 0);
	KASSERT_GT(fd, 0);
	KASSERT(fd < 1000);
	memset(buf, 0, sizeof(buf));
	n = sys_read((int)fd, buf, sizeof(buf) - 1);
	sys_close((int)fd);
	KASSERT_GT(n, 0);
	KASSERT(strstr(buf, "-") != NULL);

	{
		const char *line = buf;

		while (line && *line)
		{
			const char *p = line;
			uint64_t start;
			uint64_t end;

			if (ktest_parse_hex64(&p, &start) == 0 && *p == '-')
			{
				p++;
				if (ktest_parse_hex64(&p, &end) == 0 &&
				    start == (uint64_t)map_start &&
				    end == (uint64_t)map_end)
				{
					KASSERT(strncmp(p, " rw-p ", 6) == 0);
					found = 1;
					break;
				}
			}
			line = strchr(line, '\n');
			if (line)
				line++;
		}
	}
	KASSERT(found);
	KASSERT_EQ(sys_munmap(mapped, IR0_PAGE_SIZE), 0);

	/* /proc/self/statm: 7 space-separated page counts; first is a digit. */
	fd = sys_open("/proc/self/statm", 0, 0);
	KASSERT_GT(fd, 0);
	KASSERT(fd < 1000);
	memset(buf, 0, sizeof(buf));
	n = sys_read((int)fd, buf, sizeof(buf) - 1);
	sys_close((int)fd);
	KASSERT_GT(n, 0);
	KASSERT(buf[0] >= '0' && buf[0] <= '9');
	KASSERT(strstr(buf, " ") != NULL);

	KTEST_END();
}

void ktest_procfs_pid_fd(void)
{
	const char *path = "/proc/self/fd/0";
	char buf[256];
	char linkbuf[256];
	int64_t fd;
	int64_t n;
	int64_t link_len;

	KTEST_BEGIN("procfs_pid_fd");
	if (!current_process)
	{
		KTEST_END();
		return;
	}

	fd = sys_open(path, 0, 0);
	KASSERT_GT(fd, 0);
	KASSERT(fd < 1000);
	memset(buf, 0, sizeof(buf));
	n = sys_read((int)fd, buf, sizeof(buf) - 1);
	sys_close((int)fd);
	KASSERT_GT(n, 0);

	memset(linkbuf, 0, sizeof(linkbuf));
	link_len = sys_readlink(path, linkbuf, sizeof(linkbuf) - 1);
	KASSERT_GT(link_len, 0);
	KASSERT(strcmp(buf, linkbuf) == 0);

	KTEST_END();
}

void ktest_procfs_pid_environ(void)
{
	int64_t fd;
	char buf[512];
	int64_t n;

	KTEST_BEGIN("procfs_pid_environ");
	if (!current_process)
	{
		KTEST_END();
		return;
	}

	fd = sys_open("/proc/self/environ", 0, 0);
	KASSERT_GT(fd, 0);
	KASSERT(fd < 1000);
	memset(buf, 0, sizeof(buf));
	n = sys_read((int)fd, buf, sizeof(buf) - 1);
	sys_close((int)fd);
	KASSERT_GE(n, 0);
	if (current_process->saved_environ_len > 0)
		KASSERT_GT(n, 0);

	KTEST_END();
}

void ktest_routed_open_access(void)
{
	const char *fifo_path = "/run/ktest-access-fifo";
	uint32_t saved_euid;
	uint32_t saved_egid;
	uid_t target_uid;
	gid_t target_gid;
	stat_t st;
	int64_t fd;

	KTEST_BEGIN("routed_open_access");
	if (!current_process)
	{
		KTEST_END();
		return;
	}

	saved_euid = current_process->euid;
	saved_egid = current_process->egid;

	KASSERT_EQ(proc_pid_get_owner(current_process->task.pid,
				      &target_uid, &target_gid), 0);
	KASSERT_EQ(target_uid, (uid_t)saved_euid);
	KASSERT_EQ(proc_access_path("/proc/self/environ", target_uid), 0);
	KASSERT_EQ(proc_access_path("/proc/self/maps", target_uid), 0);
	KASSERT_EQ(proc_access_path("/proc/self/environ",
				    (uid_t)(target_uid + 1U)), -EACCES);
	KASSERT_EQ(proc_access_path("/proc/self/maps",
				    (uid_t)(target_uid + 1U)), -EACCES);
	KASSERT_EQ(proc_access_path("/proc/self/fd/0",
				    (uid_t)(target_uid + 1U)), -EACCES);
	KASSERT_EQ(proc_access_path("/proc/self/exe",
				    (uid_t)(target_uid + 1U)), -EACCES);
	KASSERT_EQ(ir0_open_access_path_routed("/proc/self/environ", O_RDONLY,
					      target_uid, target_gid), 0);
	KASSERT_EQ(ir0_open_access_path_routed("/proc/self/environ", O_RDONLY,
					      (uid_t)(target_uid + 1U),
					      target_gid), -EACCES);

	memset(&st, 0, sizeof(st));
	KASSERT_EQ(proc_stat("/proc/self/environ", &st), 0);
	KASSERT_EQ(st.st_uid, target_uid);
	KASSERT_EQ((unsigned)(st.st_mode & 0777), 0400U);

	(void)named_fifo_unlink(fifo_path);
	KASSERT_EQ(named_fifo_create_owned(fifo_path, 0600, ROOT_UID, ROOT_GID), 0);
	memset(&st, 0, sizeof(st));
	KASSERT_EQ(ir0_stat_path_routed(fifo_path, &st), 0);
	KASSERT_EQ(st.st_uid, (uid_t)ROOT_UID);
	KASSERT_EQ((unsigned)(st.st_mode & 0777), 0600U);
	current_process->euid = 1000;
	current_process->egid = 1000;
	KASSERT_EQ(sys_open("/dev/kmsg", O_RDONLY, 0), -EACCES);
	KASSERT_EQ(sys_open(fifo_path, O_RDONLY | O_NONBLOCK, 0), -EACCES);

	fd = sys_open("/proc/self/environ", O_RDONLY, 0);
	KASSERT_GT(fd, 0);
	if (fd > 0)
		sys_close((int)fd);

	current_process->euid = saved_euid;
	current_process->egid = saved_egid;
	KASSERT_EQ(named_fifo_unlink(fifo_path), 0);
	KTEST_END();
}

void ktest_procfs_self_symlink(void)
{
	pid_t pid = -1;
	const char *name;
	char linkbuf[64];
	char expect[32];
	stat_t st;
	int plen;
	int expect_len;

	KTEST_BEGIN("procfs_self_symlink");
	if (!current_process)
	{
		KTEST_END();
		return;
	}

	name = proc_resolve_path("/proc/self", &pid);
	KASSERT(name != NULL);
	KASSERT(strcmp(name, "self_link") == 0);
	KASSERT_EQ(pid, current_process->task.pid);

	expect_len = ktest_pid_to_str((int)current_process->task.pid,
				      expect, sizeof(expect));
	KASSERT_GT(expect_len, 0);

	memset(linkbuf, 0, sizeof(linkbuf));
	plen = proc_readlink("/proc/self", linkbuf, sizeof(linkbuf));
	KASSERT_EQ(plen, expect_len);
	KASSERT(strcmp(linkbuf, expect) == 0);

	{
		int64_t link_len;

		memset(linkbuf, 0, sizeof(linkbuf));
		link_len = sys_readlink("/proc/self", linkbuf, sizeof(linkbuf));
		KASSERT_EQ(link_len, expect_len);
		KASSERT(strcmp(linkbuf, expect) == 0);
	}

	memset(&st, 0, sizeof(st));
	KASSERT_EQ(proc_stat("/proc/self", &st), 0);
	KASSERT(S_ISLNK(st.st_mode));

	KTEST_END();
}

void ktest_procfs_pid_exe(void)
{
	pid_t pid = -1;
	const char *name;
	char linkbuf[512];
	stat_t st;
	int plen;

	KTEST_BEGIN("procfs_pid_exe");
	if (!current_process)
	{
		KTEST_END();
		return;
	}

	name = proc_resolve_path("/proc/self/exe", &pid);
	KASSERT(name != NULL);
	KASSERT(strcmp(name, "exe_link") == 0);
	KASSERT_EQ(pid, current_process->task.pid);

	memset(&st, 0, sizeof(st));
	KASSERT_EQ(proc_stat("/proc/self/exe", &st), 0);
	KASSERT(S_ISLNK(st.st_mode));

	memset(linkbuf, 0, sizeof(linkbuf));
	plen = proc_readlink("/proc/self/exe", linkbuf, sizeof(linkbuf));
	if (current_process->exe_path[0])
	{
		KASSERT_GT(plen, 0);
		KASSERT(strcmp(linkbuf, current_process->exe_path) == 0);
	}
	else
		KASSERT(plen < 0);

	{
		int64_t link_len;

		memset(linkbuf, 0, sizeof(linkbuf));
		link_len = sys_readlink("/proc/self/exe", linkbuf, sizeof(linkbuf));
		if (current_process->exe_path[0])
		{
			KASSERT_GT(link_len, 0);
			KASSERT(strcmp(linkbuf, current_process->exe_path) == 0);
		}
		else
			KASSERT(link_len < 0);
	}

	KTEST_END();
}

static int ktest_parse_ull_field(const char *line, const char *prefix,
				 unsigned long long *out)
{
	size_t plen;
	unsigned long long v = 0;
	size_t i;

	if (!line || !prefix || !out)
		return -1;
	plen = strlen(prefix);
	if (strncmp(line, prefix, plen) != 0)
		return -1;
	for (i = plen; line[i] >= '0' && line[i] <= '9'; i++)
		v = v * 10ULL + (unsigned long long)(line[i] - '0');
	if (i == plen)
		return -1;
	*out = v;
	return 0;
}

void ktest_procfs_stat(void)
{
	char buf[512];
	char *line;
	int64_t fd;
	int64_t n;
	unsigned long long intr = 0;
	unsigned long long ctxt = 0;

	KTEST_BEGIN("procfs_stat");
	fd = sys_open("/proc/stat", 0, 0);
	KASSERT_GT(fd, 0);
	memset(buf, 0, sizeof(buf));
	n = sys_read((int)fd, buf, sizeof(buf) - 1);
	sys_close((int)fd);
	KASSERT_GT(n, 0);
	KASSERT(strstr(buf, "cpu ") != NULL);
	line = strstr(buf, "intr ");
	KASSERT(line != NULL);
	KASSERT(ktest_parse_ull_field(line, "intr ", &intr) == 0);
	line = strstr(buf, "ctxt ");
	KASSERT(line != NULL);
	KASSERT(ktest_parse_ull_field(line, "ctxt ", &ctxt) == 0);
	KASSERT(strstr(buf, "btime ") != NULL);
	KASSERT_GT(intr, 0ULL);
	KTEST_END();
}
