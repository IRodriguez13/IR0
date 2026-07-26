# IR0 Virtual Filesystems

> **Last verified:** 2026-07-24
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

## Weak Points

- Some endpoints remain intentionally minimal and need richer semantics.
- Coverage of edge-case parsing/format compatibility still depends on runtime tests.
