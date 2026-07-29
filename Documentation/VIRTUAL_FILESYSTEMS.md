# IR0 Virtual Filesystems

> **Last verified:** 2026-07-29
> **Source of truth:** `fs/procfs.c`, `fs/sysfs.c`, `fs/devfs.c`, `fs/heartfs.c`,
> `fs/pseudo_fs_registry.c`, [`PSEUDO_FS_HEART.md`](PSEUDO_FS_HEART.md),
> [`KLOG.md`](KLOG.md) (`/proc/kmsg`, `/dev/kmsg`)

This document focuses on pseudo-filesystems exposed through VFS.

## `/heart`

IR0-only unified read-only facade (does **not** replace `/proc` or `/sys`).
See [`PSEUDO_FS_HEART.md`](PSEUDO_FS_HEART.md) for layout, gates, and ARCH-3 notes.

## `/proc`

`procfs` exposes runtime kernel and process data.

### Common Endpoints

- `/proc/meminfo`
- `/proc/uptime`
- `/proc/stat` — aggregate + `cpu0` jiffy lines for BusyBox `top` (see below)
- `/proc/loadavg`
- `/proc/version`
- `/proc/filesystems`
- `/proc/mounts`
- `/proc/drivers`
- `/proc/interrupts`
- `/proc/blockdevices`
- `/proc/partitions`
- `/proc/kmsg` — structured klog records (`klog_read_records`, see [`KLOG.md`](KLOG.md))
- `/proc/[pid]/status`
- `/proc/[pid]/cmdline`
- `/proc/[pid]/stat` — Linux `proc(5)` field order (CPU counters still mostly 0)

### `/proc/stat` (BusyBox `top`)

Registered via `pseudo_fs_register("/proc", "stat", …)` → `proc_stat_read()`.

Contract (Linux `proc_stat(5)` / BusyBox `read_cpu_jiffy`):

```text
cpu  user nice system idle iowait irq softirq steal
cpu0 user nice system idle iowait irq softirq steal
…
```

- Jiffies use `CONFIG_TICK_RATE_HZ`; idle from `clock_get_idle_milliseconds()`.
- Non-idle time is split user/system heuristically (no per-process cputime yet).
- Guest check: `python3 scripts/smoke_proc_stat_top.py` (ISD development disk +
  `top -bn1`).

### `/proc` readdir and `getdents`

`proc_readdir("/proc")` lists **numeric PID directories first**, then `pid/`, then
static registry children. Reason: `sys_getdents` uses `GETDENTS_BATCH_MAX` (24)
on a stack-sized `vfs_dirent` array; filling the batch with static names alone
left BusyBox `top`/`ps` with **no** digit dirents → `no process info in /proc`.
Static files remain openable by path even when truncated from a directory listing.

### Notes

- Data is generated at read time.
- Numeric formatting was hardened for 64-bit values.
- Opens install real `fd_table` slots (`is_pseudo`); no global virtual fds for new opens.
- Path-based readdir for `/proc`, `/proc/pid`, `/proc/pid/N` via `proc_readdir()`.
- `/proc/kmsg` mirrors the same event ring as serial (not the legacy textual-only path).

## `/dev`

`devfs` exposes kernel device entry points.

### Common Nodes

- `/dev/null`, `/dev/zero`
- `/dev/console`, `/dev/tty`
- `/dev/kmsg` — same event backend as `/proc/kmsg` on read; writes → `klog_info("USER", …)`
- `/dev/disk`
- `/dev/net`
- `/dev/audio`
- `/dev/mouse`

### Notes

- Access uses standard syscall I/O from user-style binaries.
- Device registration is routed through driver/bootstrap infrastructure.

## `/sys`

`sysfs` exposes kernel/system data in a structured filesystem namespace.

### Notes

- Error handling paths use consistent negative errno returns.
- Console and backend exposure route through facade-backed interfaces.

## In-Memory Pseudo Backends

- `tmpfs`: writable memory-backed tree with uid/gid and umask-aware create.
- `procfs`, `devfs`, `sysfs`: dynamic pseudo filesystems.

## Strengths

- Strong observability at runtime without external debug tooling.
- Consistent user-facing access model through open/read/write/stat patterns.
- Product exploration: ash + `cat /proc/kmsg` (in-kernel dbgshell removed).
- BusyBox `top -bn1` works once `/proc/stat` + digit `/proc` dirents are present.

## Weak Points

- Some endpoints remain intentionally minimal and need richer semantics.
- Coverage of edge-case parsing/format compatibility still depends on runtime tests.
- `getdents` batch size still caps how many `/proc` names a single listing returns.
- `/proc/stat` CPU breakdown is approximate until real per-task accounting exists.
