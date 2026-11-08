/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: procfs_process.c
 * Description: /proc process, pid, fd-link, environ and status entries.
 */

#include "procfs_internal.h"


/*
 * Snapshot scalar /proc fields and pin the address space while a read path
 * walks page tables. process_find_by_pid() drops the irq lock before return;
 * a concurrent reap could free process_t and mm while stat/cmdline still run.
 */
typedef struct proc_fs_snap
{
	char comm[16];
	pid_t pid;
	pid_t ppid;
	pid_t pgid;
	pid_t sid;
	int sched_prio;
	uint64_t start_ticks;
	uint64_t utime;
	uint64_t stime;
	int state;
	uid_t uid;
	gid_t gid;
	uint32_t euid;
	uint32_t egid;
	uint32_t suid;
	uint32_t sgid;
	int oom_score_adj;
	char *cmdline_copy;
	size_t cmdline_len;
	uint64_t heap_start;
	uint64_t heap_end;
	uint64_t stack_start;
	uint64_t stack_size;
	struct mmap_region *mmap_list;
	mm_struct_t *mm;
} proc_fs_snap_t;

static int proc_fs_snap_acquire(pid_t pid, proc_fs_snap_t *snap)
{
	process_t *proc;
	uint64_t irqf;

	if (!snap)
		return -EINVAL;

	memset(snap, 0, sizeof(*snap));

	irqf = (uint64_t)irq_save();
	if (pid == -1)
		proc = current_process;
	else
		proc = process_find_by_pid(pid);

	if (!proc)
	{
		irq_restore((unsigned long)irqf);
		return -ENOENT;
	}

	strncpy(snap->comm, proc->comm, sizeof(snap->comm) - 1);
	snap->comm[sizeof(snap->comm) - 1] = '\0';
	snap->pid = proc->task.pid;
	snap->ppid = proc->ppid;
	snap->pgid = proc->pgid;
	snap->sid = proc->sid;
	snap->sched_prio = proc->sched_prio;
	snap->start_ticks = proc->start_ticks;
	snap->utime = proc->utime;
	snap->stime = proc->stime;
	snap->state = (int)proc->state;
	snap->uid = proc->uid;
	snap->gid = proc->gid;
	snap->euid = proc->euid;
	snap->egid = proc->egid;
	snap->suid = proc->suid;
	snap->sgid = proc->sgid;
	snap->oom_score_adj = process_oom_score_adj_get(proc);
	snap->cmdline_copy = NULL;
	snap->cmdline_len = 0;

	if (proc->saved_cmdline && proc->saved_cmdline_len > 0)
	{
		snap->cmdline_copy = kmalloc(proc->saved_cmdline_len);
		if (snap->cmdline_copy)
		{
			memcpy(snap->cmdline_copy, proc->saved_cmdline,
			       proc->saved_cmdline_len);
			snap->cmdline_len = proc->saved_cmdline_len;
		}
	}

	if (proc->mm)
	{
		snap->mm = mm_get(proc->mm);
		if (snap->mm)
		{
			snap->heap_start = snap->mm->heap_start;
			snap->heap_end = snap->mm->heap_end;
			snap->stack_start = snap->mm->stack_start;
			snap->stack_size = snap->mm->stack_size;
			snap->mmap_list = snap->mm->mmap_list;
		}
	}

	irq_restore((unsigned long)irqf);
	return 0;
}

static void proc_fs_snap_release(proc_fs_snap_t *snap)
{
	if (!snap)
		return;

	if (snap->cmdline_copy)
	{
		kfree(snap->cmdline_copy);
		snap->cmdline_copy = NULL;
		snap->cmdline_len = 0;
	}

	if (!snap->mm)
		return;

	mm_put(snap->mm);
	snap->mm = NULL;
}

/*
 * /proc/ps: one line header then one line per process, tab-separated.
 * Header: PID\tPPID\tS\tUID\tCMD
 * State: R=runnable/running, S=sleeping(blocked), Z=zombie (§8).
 */
int proc_ps_read(char *buf, size_t count)
{
	typedef struct
	{
		pid_t pid;
		pid_t ppid;
		uid_t uid;
		char comm[16];
		int state;
	} proc_ps_row_t;

	proc_ps_row_t rows[64];
	int row_count = 0;
	process_t *p;
	uint64_t irqf;
	size_t off = 0;
	int n;
	int i;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);

	n = snprintf(buf, count, "PID\tPPID\tS\tUID\tCMD\n");
	if (n < 0)
		return -1;
	if (n >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	off += (size_t)n;

	irqf = (uint64_t)irq_save();
	for (p = process_list; p && row_count < (int)(sizeof(rows) / sizeof(rows[0]));
	     p = p->next)
	{
		rows[row_count].pid = p->task.pid;
		rows[row_count].ppid = p->ppid;
		rows[row_count].uid = p->uid;
		rows[row_count].state = (int)p->state;
		strncpy(rows[row_count].comm, p->comm, sizeof(rows[row_count].comm) - 1);
		rows[row_count].comm[sizeof(rows[row_count].comm) - 1] = '\0';
		row_count++;
	}
	irq_restore((unsigned long)irqf);

	for (i = 0; i < row_count && off < count - 1; i++)
	{
		const char *state_str = "?";
		const char *name = rows[i].comm[0] ? rows[i].comm : "(none)";

		if (rows[i].state == PROCESS_ZOMBIE)
			state_str = "Z";
		else
		{
			switch (rows[i].state)
			{
			case PROCESS_READY:   state_str = "R"; break;
			case PROCESS_RUNNING: state_str = "R"; break;
			case PROCESS_BLOCKED: state_str = "S"; break;
			default:              state_str = "?"; break;
			}
		}

		n = snprintf(buf + off, count - off,
			     "%d\t%d\t%s\t%u\t%s\n",
			     (int)rows[i].pid, (int)rows[i].ppid, state_str,
			     (unsigned)rows[i].uid, name);
		if (n < 0)
			break;
		if (n >= (int)(count - off))
		{
			n = (int)(count - off) - 1;
			off += (size_t)n;
			break;
		}
		off += (size_t)n;
	}

	if (off < count)
		buf[off] = '\0';
	return (int)off;
}

/*
 * Parse /proc path - returns entry name after /proc/, extracts PID if present.
 * Supports OSDev-style /proc/pid/N/status, legacy /proc/N/status, and
 * /proc/self/... mapped to the current process PID (Linux-style).
 */
const char *proc_parse_path(const char *path, pid_t *pid_out)
{
    if (!is_proc_path(path))
        return NULL;

    /* Resolve /proc/self without manufacturing a temporary path. */
    if (strncmp(path, "/proc/self/", 11) == 0)
    {
        const char *rest = path + 11;

        if (!current_process)
            return NULL;
        *pid_out = current_process->task.pid;
        if (strcmp(rest, "status") == 0) return "status";
        if (strcmp(rest, "cmdline") == 0) return "cmdline";
        if (strcmp(rest, "maps") == 0) return "maps";
        if (strcmp(rest, "statm") == 0) return "statm";
        if (strcmp(rest, "oom_score_adj") == 0) return "oom_score_adj";
        if (strcmp(rest, "oom_score") == 0) return "oom_score";
        if (strcmp(rest, "environ") == 0) return "environ";
        if (strcmp(rest, "exe") == 0) return "exe_link";
        if (strcmp(rest, "stat") == 0) return "stat";
        if (strcmp(rest, "fd") == 0) return "fd_dir";
        if (strncmp(rest, "fd/", 3) == 0 &&
            rest[3] >= '0' && rest[3] <= '9')
            return "fd_link";
        return NULL;
    }

    /* Skip "/proc/" prefix */
    const char *after_proc = path + 6;

    /* /proc/self — Linux symlink to /proc/<current-pid> (bare path). */
    if (strcmp(after_proc, "self") == 0)
    {
        if (!current_process)
            return NULL;
        *pid_out = current_process->task.pid;
        return "self_link";
    }

    /* Check if it's /proc/status (current process) */
    if (strncmp(after_proc, "status", 6) == 0)
    {
        *pid_out = -1;
        return "status";
    }

    /* OSDev-style: /proc/pid or /proc/pid/N or /proc/pid/N/status */
    if (strncmp(after_proc, "pid", 3) == 0 && (after_proc[3] == '\0' || after_proc[3] == '/'))
    {
        if (after_proc[3] == '\0')
        {
            *pid_out = -1;
            return "pid_dir";
        }
        /* pid/123 or pid/123/status or pid/123/cmdline */
        const char *rest = after_proc + 4;
        char *slash = strchr(rest, '/');
        if (!slash)
        {
            *pid_out = atoi(rest);
            return "pid_subdir";
        }
        char pid_str[16];
        size_t pid_len = (size_t)(slash - rest);
        if (pid_len >= sizeof(pid_str))
            return NULL;
        strncpy(pid_str, rest, pid_len);
        pid_str[pid_len] = '\0';
        *pid_out = atoi(pid_str);
        if (strncmp(slash + 1, "status", 6) == 0)
            return "status";
        if (strncmp(slash + 1, "cmdline", 7) == 0)
            return "cmdline";
        if (strncmp(slash + 1, "maps", 4) == 0)
            return "maps";
        if (strncmp(slash + 1, "statm", 5) == 0)
            return "statm";
        if (strncmp(slash + 1, "oom_score_adj", 13) == 0)
            return "oom_score_adj";
        if (strncmp(slash + 1, "oom_score", 9) == 0)
            return "oom_score";
        if (strncmp(slash + 1, "fd", 2) == 0 &&
            (slash[3] == '\0' || slash[3] == '/'))
        {
            if (slash[3] == '\0')
                return "fd_dir";
            if (slash[4] >= '0' && slash[4] <= '9')
                return "fd_link";
        }
        if (strncmp(slash + 1, "environ", 7) == 0)
            return "environ";
        if (strncmp(slash + 1, "exe", 3) == 0 && slash[4] == '\0')
            return "exe_link";
        if (strncmp(slash + 1, "stat", 4) == 0)
            return "stat";
        return NULL;
    }

    /* Legacy: /proc/[pid]/status or /proc/[pid]/cmdline */
    char *slash = strchr(after_proc, '/');
    if (slash)
    {
        char pid_str[16];
        size_t pid_len = (size_t)(slash - after_proc);
        if (pid_len < sizeof(pid_str) - 1)
        {
            strncpy(pid_str, after_proc, pid_len);
            pid_str[pid_len] = '\0';
            *pid_out = atoi(pid_str);

            if (strncmp(slash + 1, "status", 6) == 0)
                return "status";
            if (strncmp(slash + 1, "cmdline", 7) == 0)
                return "cmdline";
            if (strncmp(slash + 1, "maps", 4) == 0)
                return "maps";
            if (strncmp(slash + 1, "statm", 5) == 0)
                return "statm";
            if (strncmp(slash + 1, "oom_score_adj", 13) == 0)
                return "oom_score_adj";
            if (strncmp(slash + 1, "oom_score", 9) == 0)
                return "oom_score";
            if (strncmp(slash + 1, "fd", 2) == 0 &&
                (slash[3] == '\0' || slash[3] == '/'))
            {
                if (slash[3] == '\0')
                    return "fd_dir";
                if (slash[4] >= '0' && slash[4] <= '9')
                    return "fd_link";
            }
            if (strncmp(slash + 1, "environ", 7) == 0)
                return "environ";
            if (strncmp(slash + 1, "exe", 3) == 0 && slash[4] == '\0')
                return "exe_link";
            if (strncmp(slash + 1, "stat", 4) == 0)
                return "stat";
        }
    }
    else
    {
        /* Bare /proc/N directory (digit-only name). */
        const char *p = after_proc;
        int digits = 0;

        while (*p >= '0' && *p <= '9')
        {
            digits++;
            p++;
        }
        if (digits > 0 && *p == '\0')
        {
            *pid_out = atoi(after_proc);
            return "pid_subdir";
        }
    }

    /* Regular /proc files */
    *pid_out = -1;

    return after_proc;
}

const char *proc_resolve_path(const char *path, pid_t *pid_out)
{
    if (!pid_out)
        return NULL;

    return proc_parse_path(path, pid_out);
}

int proc_pid_get_owner(pid_t pid, uid_t *uid, gid_t *gid)
{
	process_t *proc;
	unsigned long irqf;

	if (!uid || !gid)
		return -EINVAL;

	irqf = irq_save();
	if (pid == -1)
		proc = current_process;
	else
	{
		proc = process_list;
		while (proc && proc->task.pid != pid)
			proc = proc->next;
	}
	if (!proc)
	{
		irq_restore(irqf);
		return -ENOENT;
	}

	*uid = (uid_t)proc->euid;
	*gid = (gid_t)proc->egid;
	irq_restore(irqf);
	return 0;
}

int proc_access_path(const char *path, uid_t euid)
{
	pid_t pid;
	const char *filename;
	uid_t target_euid;
	gid_t target_egid;
	int rc;

	filename = proc_resolve_path(path, &pid);
	if (!filename)
		return -ENOENT;

	if (pid == -1 && strcmp(filename, "status") != 0)
		return 0;

	rc = proc_pid_get_owner(pid, &target_euid, &target_egid);
	if (rc != 0)
		return rc;
	(void)target_egid;

	/*
	 * Linux protects these files with PTRACE_MODE_READ_FSCREDS. IR0 does
	 * not yet model the complete ptrace/capability/namespace decision;
	 * approximate it with root-or-same-effective-UID.
	 */
	if ((strcmp(filename, "environ") == 0 ||
	     strcmp(filename, "maps") == 0 ||
	     strcmp(filename, "fd_dir") == 0 ||
	     strcmp(filename, "fd_link") == 0 ||
	     strcmp(filename, "exe_link") == 0) &&
	    euid != ROOT_UID && euid != target_euid)
		return -EACCES;

	return 0;
}

int proc_is_virtual_subdir(const char *path)
{
    pid_t pid;
    const char *filename;

    filename = proc_resolve_path(path, &pid);
    if (!filename)
        return 0;

    return strcmp(filename, "pid_dir") == 0 ||
           strcmp(filename, "pid_subdir") == 0 ||
           strcmp(filename, "fd_dir") == 0 ||
           strcmp(filename, "self_link") == 0;
}

/* /proc/[pid]/status — Linux proc(5) lines (BusyBox top reads Uid:/Name:). */
int proc_status_read(char *buf, size_t count, pid_t pid)
{
	proc_fs_snap_t snap;
	const char *state_str = "?";
	const char *state_paren = "unknown";
	int len;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);

	if (proc_fs_snap_acquire(pid, &snap) != 0)
		return -ENOENT;

	switch (snap.state)
	{
		case PROCESS_READY:
		case PROCESS_RUNNING:
			state_str = "R";
			state_paren = "running";
			break;
		case PROCESS_BLOCKED:
			state_str = "S";
			state_paren = "sleeping";
			break;
		case PROCESS_ZOMBIE:
			state_str = "Z";
			state_paren = "zombie";
			break;
	}

	len = snprintf(buf, count,
		       "Name:\t%s\n"
		       "State:\t%s (%s)\n"
		       "Pid:\t%d\n"
		       "PPid:\t%d\n"
		       "Uid:\t%d\t%d\t%d\t%d\n"
		       "Gid:\t%d\t%d\t%d\t%d\n",
		       snap.comm[0] ? snap.comm : "(none)",
		       state_str, state_paren,
		       (int)snap.pid, (int)snap.ppid,
		       (int)snap.uid, (int)snap.euid, (int)snap.suid,
		       (int)snap.uid,
		       (int)snap.gid, (int)snap.egid, (int)snap.sgid,
		       (int)snap.gid);
	proc_fs_snap_release(&snap);
	if (len < 0)
		return -1;
	if (len >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	buf[len] = '\0';
	return len;
}

/*
 * /proc/[pid]/stat — Linux proc(5) field order.
 *
 * Fields 1..22 carry real process state; utime/stime are scheduler jiffies.
 * Field 7 (tty_nr) is the /dev/console device id: IR0 exposes a single console today,
 * so every process on it shares that terminal.
 */
int proc_pid_stat_read(char *buf, size_t count, pid_t pid)
{
	proc_fs_snap_t snap;
	const char *state_str = "?";
	int tty_nr = IR0_PROC_CONSOLE_TTY_NR;
	uint64_t vsize = 0;
	uint64_t rss = 0;
	int len;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);

	if (proc_fs_snap_acquire(pid, &snap) != 0)
		return -ENOENT;

	switch (snap.state)
	{
		case PROCESS_READY:   state_str = "R"; break;
		case PROCESS_RUNNING: state_str = "R"; break;
		case PROCESS_BLOCKED: state_str = "S"; break;
		case PROCESS_ZOMBIE:  state_str = "Z"; break;
	}

	if (snap.heap_end > snap.heap_start)
		vsize = snap.heap_end - snap.heap_start;
	vsize += snap.stack_size;
	{
		const struct mmap_region *r;

		for (r = snap.mmap_list; r; r = r->next)
			vsize += (uint64_t)r->length;
	}

	if (snap.mm)
		rss = mm_count_resident_user_pages(snap.mm);

	len = snprintf(buf, count,
		       "%d (%s) %s %d %d %d %d %d "     /*  1-8  */
		       "0 0 0 0 %llu %llu 0 0 0 %d "    /*  9-18 */
		       "0 1 0 %llu %llu %llu 0 0 0 0 0 0 0 0 0 0 0 0\n",
		       (int)snap.pid,
		       snap.comm[0] ? snap.comm : "none",
		       state_str,
		       (int)snap.ppid,
		       (int)snap.pgid,
		       (int)snap.sid,
		       tty_nr,
		       (int)snap.pgid,               /* tpgid: fg group on tty */
		       (unsigned long long)snap.utime,
		       (unsigned long long)snap.stime,
		       snap.sched_prio,               /* 18: priority */
		       (unsigned long long)snap.start_ticks, /* 22: starttime */
		       (unsigned long long)vsize,               /* 23: vsize */
		       (unsigned long long)rss);                /* 24: rss pages */
	proc_fs_snap_release(&snap);
	if (len < 0)
		return -1;
	if (len >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	buf[len] = '\0';
	return len;
}

/*
 * /proc/[pid]/maps — Linux proc(5) layout:
 *   <start>-<end> <perms> <offset> <dev> <inode>    <pathname>
 *
 * IR0 emits the regions it actually tracks in mm_struct: the brk heap, the
 * demand-paged mmap regions (with their real prot/shared bits), and the user
 * stack. offset/dev/inode are 0/00:00/0 because user mappings are not backed
 * by on-disk inodes yet; nothing here is fabricated.
 */
int proc_pid_maps_read(char *buf, size_t count, pid_t pid)
{
	proc_fs_snap_t snap;
	const struct mmap_region *r;
	int len = 0;
	int n;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);

	if (proc_fs_snap_acquire(pid, &snap) != 0)
		return -ENOENT;

	if (snap.heap_end > snap.heap_start)
	{
		n = snprintf(buf + len, count - (size_t)len,
			     "%016llx-%016llx rw-p 00000000 00:00 0          [heap]\n",
			     (unsigned long long)snap.heap_start,
			     (unsigned long long)snap.heap_end);
		if (n > 0 && n < (int)(count - (size_t)len))
			len += n;
	}

	for (r = snap.mmap_list; r; r = r->next)
	{
		uint64_t start = (uint64_t)(uintptr_t)r->addr;
		uint64_t end = start + (uint64_t)r->length;
		char perms[5];

		perms[0] = (r->prot & PROT_READ) ? 'r' : '-';
		perms[1] = (r->prot & PROT_WRITE) ? 'w' : '-';
		perms[2] = (r->prot & PROT_EXEC) ? 'x' : '-';
		perms[3] = (r->flags & MAP_SHARED) ? 's' : 'p';
		perms[4] = '\0';

		n = snprintf(buf + len, count - (size_t)len,
			     "%016llx-%016llx %s 00000000 00:00 0\n",
			     (unsigned long long)start,
			     (unsigned long long)end, perms);
		if (n > 0 && n < (int)(count - (size_t)len))
			len += n;
		else
			break;
	}

	if (snap.stack_size > 0)
	{
		uint64_t sstart = snap.stack_start;
		uint64_t send = sstart + snap.stack_size;

		n = snprintf(buf + len, count - (size_t)len,
			     "%016llx-%016llx rw-p 00000000 00:00 0          [stack]\n",
			     (unsigned long long)sstart,
			     (unsigned long long)send);
		if (n > 0 && n < (int)(count - (size_t)len))
			len += n;
	}

	proc_fs_snap_release(&snap);
	buf[count - 1] = '\0';
	return len;
}

/*
 * /proc/[pid]/statm — Linux proc(5): size resident shared text lib data dt
 * (units are pages). IR0 reports real total virtual size and resident pages
 * from mm_struct; shared/text/lib/data/dt are 0 (no per-segment accounting).
 */
int proc_pid_statm_read(char *buf, size_t count, pid_t pid)
{
	proc_fs_snap_t snap;
	uint64_t vsize = 0;
	uint64_t size_pages;
	uint64_t rss = 0;
	int len;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);

	if (proc_fs_snap_acquire(pid, &snap) != 0)
		return -ENOENT;

	if (snap.heap_end > snap.heap_start)
		vsize = snap.heap_end - snap.heap_start;
	vsize += snap.stack_size;
	{
		const struct mmap_region *r;

		for (r = snap.mmap_list; r; r = r->next)
			vsize += (uint64_t)r->length;
	}
	if (snap.mm)
		rss = mm_count_resident_user_pages(snap.mm);

	size_pages = vsize / (uint64_t)IR0_MM_PAGE_SIZE;

	len = snprintf(buf, count, "%llu %llu 0 0 0 0 0\n",
		       (unsigned long long)size_pages,
		       (unsigned long long)rss);
	proc_fs_snap_release(&snap);
	if (len < 0)
		return -1;
	if (len >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	buf[len] = '\0';
	return len;
}

/*
 * /proc/[pid]/oom_score_adj — Linux proc(5): signed bias in [-1000, 1000].
 * IR0_OOM_SCORE_ADJ_MIN (-1000) removes the task from OOM consideration.
 */
int proc_pid_oom_score_adj_read(char *buf, size_t count, pid_t pid)
{
	proc_fs_snap_t snap;
	int len;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);

	if (proc_fs_snap_acquire(pid, &snap) != 0)
		return -ENOENT;

	len = snprintf(buf, count, "%d\n", snap.oom_score_adj);
	proc_fs_snap_release(&snap);
	if (len < 0)
		return -1;
	if (len >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	buf[len] = '\0';
	return len;
}

/*
 * /proc/[pid]/oom_score_adj write. Only the task itself or a privileged
 * (euid 0) task may change the bias, mirroring the Linux capability check.
 */
int proc_pid_oom_score_adj_write(pid_t pid, const char *buf, size_t count)
{
	process_t *target;
	char value_buf[16];
	long value;
	char *end;
	int rc;

	if (!buf || count == 0)
		return -EINVAL;
	if (!current_process)
		return -ESRCH;
	/* Never accept a valid prefix while silently discarding excess input. */
	if (count >= sizeof(value_buf))
		return -EINVAL;

	memcpy(value_buf, buf, count);
	value_buf[count] = '\0';

	value = strtol(value_buf, &end, 10);
	if (end == value_buf)
		return -EINVAL;
	while (*end == '\n' || *end == '\r' || *end == ' ' || *end == '\t')
		end++;
	if (*end != '\0')
		return -EINVAL;
	if (value < IR0_OOM_SCORE_ADJ_MIN || value > IR0_OOM_SCORE_ADJ_MAX)
		return -EINVAL;

	if (pid == -1)
		pid = current_process->task.pid;
	target = process_find_by_pid(pid);
	if (!target)
		return -ENOENT;
	if (target != current_process && current_process->euid != 0)
		return -EPERM;
	/*
	 * Linux requires CAP_SYS_RESOURCE to lower oom_score_adj. Without this
	 * check an unprivileged task can write -1000 to /proc/self and make
	 * itself permanently immune to global OOM selection.
	 */
	if (value < process_oom_score_adj_get(target) &&
	    current_process->euid != 0)
		return -EPERM;

	rc = process_oom_score_adj_set(target, (int)value);
	if (rc != 0)
		return rc;
	return (int)count;
}

/*
 * /proc/[pid]/oom_score — Linux proc(5): badness normalised to 0..1000.
 * Computed from a pinned snapshot so a concurrent reap cannot free the mm
 * while the page-table walk runs.
 */
int proc_pid_oom_score_read(char *buf, size_t count, pid_t pid)
{
	proc_fs_snap_t snap;
	uint64_t resident = 0;
	uint64_t tables = 0;
	uint64_t score;
	int len;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);

	if (proc_fs_snap_acquire(pid, &snap) != 0)
		return -ENOENT;

	if (snap.mm)
		mm_count_user_pages(snap.mm, &resident, &tables);
	score = oom_badness_score(resident, tables, snap.oom_score_adj,
				  (uint64_t)pmm_total_frames());
	proc_fs_snap_release(&snap);

	len = snprintf(buf, count, "%llu\n", (unsigned long long)score);
	if (len < 0)
		return -1;
	if (len >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	buf[len] = '\0';
	return len;
}

int proc_fd_snap_acquire(pid_t pid, proc_fd_snap_t *snap)
{
	process_t *proc;
	unsigned long irqf;

	if (!snap)
		return -EINVAL;

	memset(snap, 0, sizeof(*snap));

	irqf = irq_save();
	if (pid == -1)
		proc = current_process;
	else
	{
		proc = process_list;
		while (proc && proc->task.pid != pid)
			proc = proc->next;
	}

	if (proc && proc->files && files_struct_live(proc->files))
	{
		memcpy(snap->table, proc->files->fd_table, sizeof(snap->table));
		snap->valid = 1;
	}
	irq_restore(irqf);
	return snap->valid;
}

static int proc_parse_pid_fd_link(const char *path, pid_t *pid_out, int *fd_out)
{
	const char *name;
	const char *fd_slash;
	int fd;

	if (!path || !pid_out || !fd_out)
		return -EINVAL;

	name = proc_parse_path(path, pid_out);
	if (!name || strcmp(name, "fd_link") != 0)
		return -ENOENT;

	fd_slash = strstr(path, "/fd/");
	if (!fd_slash)
		return -ENOENT;

	fd = atoi(fd_slash + 4);
	if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
		return -EINVAL;

	*fd_out = fd;
	return 0;
}

static int proc_fd_entry_target(const fd_entry_t *ent, char *buf, size_t count)
{
	if (!ent || !ent->in_use)
		return -ENOENT;

	if (ent->path[0] != '\0')
		return snprintf(buf, count, "%s", ent->path);
	if (ent->is_pipe)
		return snprintf(buf, count, "[pipe]");
	if (ent->is_socket)
		return snprintf(buf, count, "[socket]");
	if (ent->is_epoll)
		return snprintf(buf, count, "[eventpoll]");
	if (ent->is_eventfd)
		return snprintf(buf, count, "[eventfd]");
	if (ent->is_timerfd)
		return snprintf(buf, count, "[timerfd]");
	if (ent->is_memfd)
		return snprintf(buf, count, "[memfd]");
	if (ent->is_devfs)
		return snprintf(buf, count, "[dev]");
	if (ent->is_pseudo)
		return snprintf(buf, count, "[pseudo]");
	return snprintf(buf, count, "[anon]");
}

int proc_pid_fd_link_target_read(char *buf, size_t count, pid_t pid, int fd_num)
{
	proc_fd_snap_t snap;
	int len;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;

	if (!proc_fd_snap_acquire(pid, &snap))
		return -ENOENT;

	if (fd_num < 0 || fd_num >= MAX_FDS_PER_PROCESS ||
	    !snap.table[fd_num].in_use)
		return -ENOENT;

	len = proc_fd_entry_target(&snap.table[fd_num], buf, count);
	if (len < 0)
		return len;
	if ((size_t)len >= count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	buf[len] = '\0';
	return len;
}

int proc_pid_exe_link_target_read(char *buf, size_t count, pid_t pid)
{
	process_t *proc;
	unsigned long irqf;
	int len;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;

	irqf = irq_save();
	if (pid == -1)
		proc = current_process;
	else
	{
		proc = process_list;
		while (proc && proc->task.pid != pid)
			proc = proc->next;
	}

	if (!proc || !proc->exe_path[0])
	{
		irq_restore(irqf);
		return -ENOENT;
	}

	len = snprintf(buf, count, "%s", proc->exe_path);
	irq_restore(irqf);
	if (len < 0)
		return -1;
	if ((size_t)len >= count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	buf[len] = '\0';
	return len;
}

#define PROC_ENVIRON_VARS_MAX 256
#define PROC_ENVIRON_ENTRY_MAX 4096

/*
 * Linux /proc/pid/environ reflects the process entry envp[] (live user memory),
 * not only the exec-time snapshot. OpenRC's proc mount test compares md5sum of
 * /proc/self/environ with VAR=a vs VAR=b — they must differ.
 */
static int proc_environ_read_live(const process_t *proc, char *buf, size_t count)
{
	uint64_t envp_array;
	size_t pos = 0;
	int i;

	if (!proc || proc->mode != USER_MODE || proc != current_process)
		return 0;

	envp_array = proc->exec_envp_user;
	if (!envp_array ||
	    !is_user_address((const void *)(uintptr_t)envp_array, sizeof(uint64_t)))
		return 0;

	for (i = 0; i < PROC_ENVIRON_VARS_MAX; i++)
	{
		uint64_t str_ptr = 0;
		char entry[PROC_ENVIRON_ENTRY_MAX];
		const void *slot = (const void *)(uintptr_t)(envp_array +
							     (uint64_t)i *
							     sizeof(uint64_t));
		size_t len;

		if (!is_user_address_checked(slot, sizeof(uint64_t), 1))
			break;
		if (copy_from_user(&str_ptr, slot, sizeof(str_ptr)) != 0)
			break;
		if (str_ptr == 0)
			break;
		if (!is_user_address_checked((const void *)(uintptr_t)str_ptr, 1, 1))
			break;
		if (copy_from_user_cstring(entry, sizeof(entry),
					   (const char *)(uintptr_t)str_ptr) != 0)
			break;

		len = strlen(entry) + 1;
		if (pos + len > count)
		{
			memcpy(buf + pos, entry, count - pos);
			return (int)count;
		}
		memcpy(buf + pos, entry, len);
		pos += len;
	}

	return (int)pos;
}

int proc_pid_environ_read(char *buf, size_t count, pid_t pid)
{
	process_t *proc;
	char *blob;
	size_t blob_len;
	unsigned long irqf;
	int live_len;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;

	memset(buf, 0, count);

	irqf = irq_save();
	if (pid == -1)
		proc = current_process;
	else
	{
		proc = process_list;
		while (proc && proc->task.pid != pid)
			proc = proc->next;
	}

	if (!proc)
	{
		irq_restore(irqf);
		return 0;
	}

	if (proc == current_process && proc->mode == USER_MODE)
	{
		irq_restore(irqf);
		live_len = proc_environ_read_live(proc, buf, count);
		if (live_len > 0)
			return live_len;
		irqf = irq_save();
	}

	if (!proc->saved_environ || proc->saved_environ_len == 0)
	{
		irq_restore(irqf);
		return 0;
	}

	blob = proc->saved_environ;
	blob_len = proc->saved_environ_len;
	irq_restore(irqf);

	if (blob_len > count)
	{
		memcpy(buf, blob, count);
		return (int)count;
	}

	memcpy(buf, blob, blob_len);
	return (int)blob_len;
}

static int proc_readlink_finish(char *buf, size_t buflen, int len)
{
	if (len < 0)
		return len;
	if ((size_t)len >= buflen)
	{
		buf[buflen - 1] = '\0';
		return (int)(buflen - 1);
	}
	buf[len] = '\0';
	return len;
}

int proc_readlink(const char *path, char *buf, size_t buflen)
{
	pid_t pid;
	const char *name;
	int fd_num;
	int len;

	if (!path || !buf || buflen == 0)
		return -EINVAL;

	if (proc_parse_pid_fd_link(path, &pid, &fd_num) == 0)
	{
		len = proc_pid_fd_link_target_read(buf, buflen, pid, fd_num);
		return proc_readlink_finish(buf, buflen, len);
	}

	name = proc_parse_path(path, &pid);
	if (!name)
		return -ENOENT;

	if (strcmp(name, "self_link") == 0)
	{
		if (!current_process)
			return -ENOENT;
		len = snprintf(buf, buflen, "%d", (int)current_process->task.pid);
		return proc_readlink_finish(buf, buflen, len);
	}

	if (strcmp(name, "exe_link") == 0)
	{
		len = proc_pid_exe_link_target_read(buf, buflen, pid);
		return proc_readlink_finish(buf, buflen, len);
	}

	return -ENOENT;
}

/* Generate /proc/[pid]/cmdline content (NUL-separated argv, Linux proc(5)). */
int proc_cmdline_read(char *buf, size_t count, pid_t pid)
{
	proc_fs_snap_t snap;
	size_t n;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;

	memset(buf, 0, count);

	if (proc_fs_snap_acquire(pid, &snap) != 0)
		return -ENOENT;

	if (!snap.cmdline_copy || snap.cmdline_len == 0)
	{
		proc_fs_snap_release(&snap);
		return 0;
	}

	n = snap.cmdline_len;
	if (n > count)
		n = count;
	memcpy(buf, snap.cmdline_copy, n);
	proc_fs_snap_release(&snap);

	if (n >= count && count > 0)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	return (int)n;
}
