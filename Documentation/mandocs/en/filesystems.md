# IR0 Filesystems (Backends)

| Field | Value |
|-------|-------|
| Version | 0.1 |
| IR0 phase | T0 |
| Status | stable |
| Depends on | vfs, syscalls, drivers |
| Man page | IR0-filesystems (section 7) |
| Primary sources | `fs/tmpfs.c`, `fs/devfs.c`, `fs/procfs.c`, `fs/pseudo_fs_registry.c`, `fs/minix_fs.c`, `includes/ir0/sysfs.h` |

## 1. Overview

IR0 exposes several filesystem backends with different routing and backing models.
Block-backed **minix** and in-memory **tmpfs** register as `vfs_fstype` drivers.
**procfs**, **sysfs**, and **devfs** are primarily **syscall-side** namespaces
(not full VFS mounts). Static `/proc` and `/sys` nodes also use
`pseudo_fs_registry.c` for longest-prefix dispatch.

See IR0-vfs for the two-stage router diagram.

## 2. Internal architecture

| Backend | Router | Storage | Key file |
|---------|--------|---------|----------|
| minix | VFS mount | Block device (ATA) | `fs/minix_fs.c` |
| tmpfs | VFS mount | RAM inode tree | `fs/tmpfs.c` |
| procfs | Syscall + registry | Generated kernel text | `fs/procfs.c` |
| sysfs | Syscall + registry | Kernel/driver state | `includes/ir0/sysfs.h` |
| devfs | Syscall only | Node ops table | `fs/devfs.c` |

**devfs node:** `devfs_node_t` with `device_id`, `ref_count`, optional `ops`
(read/write/ioctl/can_read hooks). Registry max **224** nodes.

**pseudo_fs_registry:** separate tables for `/proc` and `/sys`; fd bases 1500 and 3500; max 64 static entries each, 16 dynamic matchers.

## 3. Data flow

```text
  open("/etc/passwd")     → VFS → minix → block_dev → ATA
  open("/tmp/x")          → VFS → tmpfs → RAM inode
  open("/proc/meminfo")   → proc_open → pseudo_fs or procfs generator
  open("/sys/...")        → sysfs_open → registry ops
  open("/dev/console")    → devfs_find_node → console_ops → ir0_console_*
  open("/dev/fb0")        → devfs → fb mmap path in sys_mmap
```

**Endpoint classification:**

```text
  ┌─────────────┬──────────┬─────────────────────┐
  │ Prefix      │ Backing  │ Hardware?           │
  ├─────────────┼──────────┼─────────────────────┤
  │ / (minix)   │ disk     │ yes (block_dev)     │
  │ /tmp tmpfs  │ RAM      │ no                  │
  │ /proc       │ generated│ no                  │
  │ /sys        │ mixed    │ sometimes (CPU info)│
  │ /dev/null   │ sink     │ no                  │
  │ /dev/fb0    │ driver   │ yes (framebuffer)   │
  │ /dev/events0│ input    │ yes (keyboard/mouse)│
  └─────────────┴──────────┴─────────────────────┘
```

## 4. Responsibilities

- **minix/tmpfs:** implement `vfs_ops`; enforce backend limits and permissions.
- **procfs:** generate text at read time; per-process fd context where needed.
- **devfs:** register nodes at init; refcount on open/close; poll hooks per device.
- **Registry:** longest-prefix match; no duplicate full_path registration.

## 5. Subsystem boundaries

- Backends must not include syscalls or process-specific harness code (`vfs_backend.h`).
- procfs reads kernel state through `includes/ir0/*` facades, not raw driver headers in new code.
- devfs ioctl user copies whitelisted in `architecture_guard.py` for console/fb/audio.

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|---------------|
| VFS | minix/tmpfs registered in `vfs_init` |
| Drivers | devfs nodes for disk, net, fb, input |
| Process | proc pid directories; fd owner maps for pseudo fds |
| Block | minix LBA via `ir0/block_dev.h` |

## 7. Visual maps

```text
           syscall open path
                 │
     ┌───────────┼───────────┐
     ▼           ▼           ▼
  procfs      devfs        VFS mount table
     │           │           │
  registry    node ops    minix / tmpfs
     │           │           │
  kernel      drivers     block / RAM
  state       facades
```

## 8. Important invariants

1. tmpfs: **128 files/instance**, **64 KiB/file**, **32 mount instances**.
2. `ramfs` fstype aliases to tmpfs at `vfs_mount`.
3. proc pseudo fds 1000–1999 with per-owner PID map; sysfs offsets 3000–3999.
4. minix is default root (`CONFIG_ROOT_FILESYSTEM="minix"`).
5. Negative errno throughout all backends.

## 9. Debugging tips

- `/proc/mounts`, `/proc/filesystems`, `/proc/drivers` — live introspection.
- devfs open fails `-ENOENT`: node not registered in `devfs_register_node`.
- tmpfs `-ENOSPC`: file count or 64 KiB cap hit.
- MINIX root fail → tmpfs fallback (serial from `vfs_init_root`).

## 10. Future roadmap

- Unified VFS registration for proc/dev/sys (today: dual router debt).
- ext2/simplefs/fat16 exist as optional VFS drivers; not production root default.
- Richer permission model on pseudo nodes (future chmod semantics).
- Process-local mount namespaces — **not implemented**.

Legacy: `Documentation/FILESYSTEM.md`, `Documentation/VIRTUAL_FILESYSTEMS.md`.
