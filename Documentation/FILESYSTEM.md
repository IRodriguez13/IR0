# IR0 Filesystem Architecture

> **Last verified:** 2026-07-10  
> **Source of truth:** `fs/vfs.c`, `fs/fat16_*.c`, `includes/ir0/blockdev.h`,  
> [`BACKLOG_REMAINING.md`](BACKLOG_REMAINING.md)

IR0 uses a VFS-first design where policy is centralized and backend filesystems
provide concrete operations.

## Active Layers

1. `fs/vfs.c`: path resolution, mount dispatch, open/read/write/chmod/chown flow.
2. Backend filesystems:
   - persistent: `minix`, **FAT16 read-only** (`fs/fat16_disk.c` on block devices)
   - in-memory: `tmpfs`, virtual `fat0` (simplefs engine)
   - pseudo: `procfs`, `devfs`, `sysfs`, **`/heart`** (`fs/heartfs.c`)
3. Storage via `ir0_block_*` facade (`includes/ir0/blockdev.h`); ATA registers backends.

## Current Filesystem Set

- Root filesystem: selected by config through `vfs_init_root()` (typically MINIX).
- `procfs` / `sysfs` / `devfs`: pseudo mounts (registry + fd_table binds).
- `/heart`: IR0 facade reexporting proc/sys + kernel meta + embedded sources.
- `tmpfs`: volatile files and directories with uid/gid and umask behavior.
- `minix`: disk-backed baseline filesystem (rw path audited via vfs-write bundle).
- `fat16`: on-disk **read-only** MVP (`smoke-fat16-mount` on `/dev/hdb`).

## Permission Model in Path

- Access checks are done against effective credentials (`euid`, `egid`).
- `chmod` policy: owner-or-root at syscall and VFS boundary.
- `chown` policy: root-only at syscall and VFS boundary.
- Backends (`tmpfs`, `minix`) also enforce policy to avoid bypasses.

## Semantics and Behavior

- Negative errno is returned consistently for failure paths.
- `O_TRUNC` is supported through VFS truncate operation dispatch.
- Relative paths are resolved against per-process `cwd`.
- `/proc` per-process contexts avoid pseudo-fd collisions across processes.

## Strengths

- Clear separation between VFS policy and backend implementation.
- Configurable root/backend composition via Kconfig and Makefile wiring.
- Good observability through pseudo-filesystem endpoints.

## Weak Points

- Backend parity is still evolving for advanced Unix semantics.
- FAT16 write, EXT2, AHCI/NVMe, GPT — see [`BACKLOG_REMAINING.md`](BACKLOG_REMAINING.md).
- Some metadata and edge-case behavior remains hobby-kernel grade.
- Heavy runtime correctness depends on broad integration testing.

