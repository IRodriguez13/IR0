/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: procfs.c
 * Description: Minimal /proc filesystem dispatcher, readdir and legacy fds.
 */

#include "procfs_internal.h"


/* Check if path is in /proc (mount root or under it). */
bool is_proc_path(const char *path)
{
    if (!path)
        return false;
    if (strcmp(path, "/proc") == 0 || strcmp(path, "/proc/") == 0)
        return true;
    return strncmp(path, "/proc/", 6) == 0;
}

/* Legacy virtual-fd offset maps removed — offsets live in process fd_table. */
off_t proc_get_offset(int fd)
{
    (void)fd;
    return 0;
}

static int proc_readdir_add(struct vfs_dirent *entries, int max_entries, int n,
                            const char *name, uint8_t type)
{
    if (n < 0 || n >= max_entries || !name || !name[0])
        return n;

    strncpy(entries[n].name, name, sizeof(entries[n].name) - 1);
    entries[n].name[sizeof(entries[n].name) - 1] = '\0';
    entries[n].type = type;
    return n + 1;
}

static int proc_readdir_fill_pid_subdir(struct vfs_dirent *entries, int max_entries,
                                        int n)
{
	n = proc_readdir_add(entries, max_entries, n, "status", DT_REG);
	n = proc_readdir_add(entries, max_entries, n, "cmdline", DT_REG);
	n = proc_readdir_add(entries, max_entries, n, "stat", DT_REG);
	n = proc_readdir_add(entries, max_entries, n, "maps", DT_REG);
	n = proc_readdir_add(entries, max_entries, n, "statm", DT_REG);
	n = proc_readdir_add(entries, max_entries, n, "oom_score_adj", DT_REG);
	n = proc_readdir_add(entries, max_entries, n, "oom_score", DT_REG);
	n = proc_readdir_add(entries, max_entries, n, "fd", DT_DIR);
	n = proc_readdir_add(entries, max_entries, n, "environ", DT_REG);
	n = proc_readdir_add(entries, max_entries, n, "exe", DT_LNK);
	return n;
}

/*
 * Snapshot live PIDs under irq_save, then fill entries — same pattern as
 * proc_ps_read. Avoids UAF if a concurrent exit unlinks process_list while
 * ls/getdents walks /proc (session soak: ls /proc | head → shell SIGSEGV).
 */
static int proc_readdir_fill_live_pids(struct vfs_dirent *entries, int max_entries,
                                       int n, int skip_zombies)
{
    pid_t snap[64];
    int snap_count = 0;
    process_t *p;
    unsigned long irqf;
    int i;

    if (!entries || max_entries <= 0 || n < 0)
        return n;

    irqf = irq_save();
    for (p = process_list;
         p && snap_count < (int)(sizeof(snap) / sizeof(snap[0])) &&
         n + snap_count < max_entries;
         p = p->next)
    {
        if (skip_zombies && p->state == PROCESS_ZOMBIE)
            continue;
        snap[snap_count++] = p->task.pid;
    }
    irq_restore(irqf);

    for (i = 0; i < snap_count && n < max_entries; i++)
    {
        char pid_str[16];
        int len;

        len = snprintf(pid_str, sizeof(pid_str), "%d", (int)snap[i]);
        if (len <= 0 || len >= (int)sizeof(pid_str))
            break;
        n = proc_readdir_add(entries, max_entries, n, pid_str, DT_DIR);
    }

    return n;
}

/*
 * Path-based readdir for /proc mounts (no virtual fds).
 * /proc          — digit PIDs first, then "pid", then registry children
 * /proc/pid      — one dirent per live PID
 * /proc/pid/N    — status, cmdline, stat
 *
 * Digit PIDs must come first: sys_getdents uses GETDENTS_BATCH_MAX (24) and
 * truncates; BusyBox top/ps scan only numeric dirents then open /proc/N/stat.
 * Filling the batch with static registry names alone yields "no process info".
 */
int proc_readdir(const char *path, struct vfs_dirent *entries, int max_entries)
{
    pid_t pid;
    const char *filename;
    int n;

    if (!path || !entries || max_entries <= 0)
        return -EINVAL;

    if (strcmp(path, "/proc") == 0 || strcmp(path, "/proc/") == 0)
    {
        pseudo_fs_nodes_register_all();
        n = 0;
        n = proc_readdir_fill_live_pids(entries, max_entries, n, 1);
        n = proc_readdir_add(entries, max_entries, n, "self", DT_LNK);
        n = proc_readdir_add(entries, max_entries, n, "pid", DT_DIR);
        if (n < max_entries)
        {
            int reg;

            reg = pseudo_fs_collect_registry_children("/proc", entries,
                                                     max_entries, n);
            if (reg < 0)
                return reg;
            n = reg;
        }
        return n;
    }

    if (strcmp(path, "/proc/self") == 0 || strcmp(path, "/proc/self/") == 0)
    {
        if (!current_process)
            return -ENOENT;
        pid = current_process->task.pid;
        if (!process_find_by_pid(pid))
            return -ENOENT;
        n = 0;
        return proc_readdir_fill_pid_subdir(entries, max_entries, n);
    }

    filename = proc_resolve_path(path, &pid);
    if (!filename)
        return -ENOENT;

    if (strcmp(filename, "pid_dir") == 0)
    {
        n = 0;
        n = proc_readdir_fill_live_pids(entries, max_entries, n, 0);
        return n;
    }

    if (strcmp(filename, "pid_subdir") == 0)
    {
        if (!process_find_by_pid(pid))
            return -ENOENT;
        n = 0;
        return proc_readdir_fill_pid_subdir(entries, max_entries, n);
    }

    if (strcmp(filename, "fd_dir") == 0)
    {
        proc_fd_snap_t snap;
        int i;

        if (!process_find_by_pid(pid))
            return -ENOENT;
        if (!proc_fd_snap_acquire(pid, &snap))
            return 0;

        n = 0;
        for (i = 0; i < MAX_FDS_PER_PROCESS && n < max_entries; i++)
        {
            char fd_name[16];

            if (!snap.table[i].in_use)
                continue;
            if (snprintf(fd_name, sizeof(fd_name), "%d", i) <= 0)
                continue;
            n = proc_readdir_add(entries, max_entries, n, fd_name, DT_LNK);
        }
        return n;
    }

    return -ENOTDIR;
}

/* Legacy fd-based getdents — kept for transitional callers; prefer proc_readdir. */
int proc_getdents(int fd, void *dirent_buf, size_t count)
{
    (void)fd;
    (void)dirent_buf;
    (void)count;
    return -EBADF;
}

/* Legacy virtual-fd offset maps removed — offsets live in process fd_table. */
void proc_set_offset(int fd, off_t offset)
{
    (void)fd;
    (void)offset;
}

/* Open /proc — no longer assigns global virtual fds (use fd_table binds). */
int proc_open(const char *path, int flags)
{
    pid_t pid;
    const char *filename;

    (void)flags;

    if (!is_proc_path(path))
        return -EINVAL;

    pseudo_fs_nodes_register_all();

    filename = proc_parse_path(path, &pid);
    if (!filename)
        return -ENOENT;

    /* Files: opened only via pseudo_bind_file_fd (registry / dynamic). */
    if (strcmp(filename, "status") == 0 || strcmp(filename, "cmdline") == 0 ||
        (strcmp(filename, "stat") == 0 && pid > 0))
        return -ENOENT;

    if (strcmp(filename, "pid_dir") == 0 || strcmp(filename, "pid_subdir") == 0 ||
        strcmp(filename, "self_link") == 0)
        return -EISDIR;

    /* Static registry nodes also use bind path, not this helper. */
    return -ENOENT;
}

/* Read from /proc file — syscall path uses process fd_table is_pseudo binds. */
int proc_read(int fd, char *buf, size_t count, off_t offset)
{
	(void)fd;
	(void)buf;
	(void)count;
	(void)offset;
	return -EBADF;
}

/*
 * proc_write - Write to /proc file entry
 *
 * Most /proc nodes are read-only. Writable paths are explicit (e.g.
 * /proc/bluetooth/scan). Writes under /proc/sys/ are not implemented and
 * return -EOPNOTSUPP.
 */
/* Write to /proc — syscall uses fd_table binds; legacy global fds retired. */
int proc_write(int fd, const char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -EINVAL;

    if (count == 0)
        return 0;

    if (fd < 1000)
        return -EBADF;

    switch (fd)
    {
        default:
            /*
             * Resolve path from the opener's fd table; writes under the
             * proc/sys virtual tree are not supported (no sysctl knobs in-tree).
             */
            {
                if (!current_process)
                    return -ESRCH;

                if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
                    return -EBADF;

                const char *path = NULL;
                fd_entry_t *fdt = process_fd_table(current_process);

                if (fdt && fdt[fd].in_use)
                    path = fdt[fd].path;

                if (!path || strncmp(path, "/proc/", 6) != 0)
                    return -EACCES;

                path += 6;
                if (strncmp(path, "sys/", 4) == 0)
                    return -EOPNOTSUPP;

                return -EACCES;
            }
    }
}

/* Get stat for /proc file */
int proc_stat(const char *path, stat_t *st)
{
    pid_t pid;
    const char *filename;
    uid_t owner_uid;
    gid_t owner_gid;
    int owner_rc;

    if (!st || !is_proc_path(path))
        return -EINVAL;

    if (strcmp(path, "/proc") == 0 || strcmp(path, "/proc/") == 0)
    {
        memset(st, 0, sizeof(stat_t));
        st->st_mode = S_IFDIR | 0555;
        st->st_nlink = 2;
        pseudo_fs_stat_now(st);
        return 0;
    }

    filename = proc_parse_path(path, &pid);
    if (filename)
    {
        if (strcmp(filename, "pid_dir") == 0 ||
            strcmp(filename, "pid_subdir") == 0 ||
            strcmp(filename, "fd_dir") == 0 ||
            strcmp(filename, "self_link") == 0)
        {
            owner_uid = ROOT_UID;
            owner_gid = ROOT_GID;
            owner_rc = proc_pid_get_owner(pid, &owner_uid, &owner_gid);
            if (owner_rc != 0)
                return owner_rc;
            memset(st, 0, sizeof(stat_t));
            if (strcmp(filename, "self_link") == 0)
                st->st_mode = S_IFLNK | 0777;
            else
                st->st_mode = S_IFDIR | 0555;
            st->st_nlink = (strcmp(filename, "self_link") == 0) ? 1 : 2;
            st->st_uid = owner_uid;
            st->st_gid = owner_gid;
            st->st_size = 0;
            pseudo_fs_stat_now(st);
            return 0;
        }

        if (strcmp(filename, "fd_link") == 0 ||
            strcmp(filename, "exe_link") == 0)
        {
            owner_rc = proc_pid_get_owner(pid, &owner_uid, &owner_gid);
            if (owner_rc != 0)
                return owner_rc;
            memset(st, 0, sizeof(stat_t));
            st->st_mode = S_IFLNK | 0777;
            st->st_nlink = 1;
            st->st_uid = owner_uid;
            st->st_gid = owner_gid;
            st->st_size = 0;
            pseudo_fs_stat_now(st);
            return 0;
        }

        if (strcmp(filename, "environ") == 0 && pid > 0)
        {
            owner_rc = proc_pid_get_owner(pid, &owner_uid, &owner_gid);
            if (owner_rc != 0)
                return owner_rc;
            memset(st, 0, sizeof(stat_t));
            st->st_mode = S_IFREG | 0400;
            st->st_nlink = 1;
            st->st_uid = owner_uid;
            st->st_gid = owner_gid;
            st->st_size = 0;
            pseudo_fs_stat_now(st);
            return 0;
        }
    }

    pseudo_fs_nodes_register_all();

    {
        const pseudo_fs_entry_t *pf;
        int st_rc;

        pf = pseudo_fs_lookup(path);
        if (pf && pf->ops && pf->ops->stat)
            return pf->ops->stat(pf->ctx, st);

        st_rc = pseudo_fs_stat_path(path, st);
        if (st_rc == 0)
            return 0;
        if (st_rc != -ENOENT)
            return st_rc;
    }

    if (!filename)
        return -ENOENT;

    /*
     * Intermediate registry directories (e.g. /proc/net, /proc/bluetooth):
     * no exact node, but they contain registered leaves. Report them as real
     * directories with a live timestamp instead of ENOENT so `stat`/`ls -ld`
     * behave like Linux.
     */
    if (pseudo_fs_path_has_children(path))
    {
        memset(st, 0, sizeof(stat_t));
        st->st_mode = S_IFDIR | 0555;
        st->st_nlink = 2;
        pseudo_fs_stat_now(st);
        return 0;
    }

    /* File not found */
    return -ENOENT;
}
