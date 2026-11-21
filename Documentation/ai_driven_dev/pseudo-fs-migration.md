# Pseudo filesystems as `vfs_fstype` mounts (decision Q3)

> **Last verified:** 2026-11-21
> **Source of truth:** `fs/vfs*.c`, `fs/vfs.h`, `includes/ir0/vfs_backend.h`,
> `fs/path_routed.c`, `fs/pseudo_fs_registry.c`, `fs/pseudo_fs_nodes.c`,
> `fs/named_*.c`, `fs/procfs*.c`, `fs/sysfs.c`, `fs/sysfs/sysfs.c`,
> `fs/devfs*.c`,
> `fs/heartfs/heartfs.c`, `kernel/syscalls/fs_syscalls.c`,
> `kernel/syscalls/fs_dirent.c`

Goal: proc, devfs, sysfs and heartfs become ordinary `struct vfs_fstype`
mounts; the `named_*` side tables become nodes of the filesystem that holds
their parent directory; `fs/pseudo_fs_registry.c`, `fs/path_routed.c` and
`fs/named_*.c` are deleted at the end.

## 1. How a path reaches a filesystem today

### Real filesystems (tmpfs, minix, ext2, fat16, simplefs, 9p)

- Each one defines a `struct vfs_fstype` (`name`, `fs_flags`, `ops`,
  `mount`, `umount`, `statfs`). `scripts/make/fs.mk` lists them in
  `<generated/filesystems.h>`; `vfs_init()` (`fs/vfs_mount.c`) registers
  them in that order after the core's `proc` stub.
- `vfs_mount(dev, path, type)` adds a `struct vfs_mount` (path,
  `backend_path`, `dev`, `fs`, `flags`). The root is mounted by
  `vfs_init_root()` from `boot_rootfs_init()`; `/tmp`, `/run`, … by
  `fs/bpivot.c`; the rest by `mount(2)`.
- Lookup is by string, not by inode: `vfs_find_mount()` picks the mount
  with the longest path prefix, `vfs_backend_path()` rewrites the path for
  that mount (`backend_path` + suffix), and the `vfs_ops` callback runs on
  it under the fs-op lock (`vfs_ops_*` in `fs/vfs.c`).
- `struct vfs_ops` (`includes/ir0/vfs_backend.h`) is the IR0 equivalent of
  Linux `inode_operations` + `file_operations`: `stat` is lookup/getattr,
  `readdir` iterate, `read`/`write` the file ops, `open` the
  `file_operations.open` hook, plus `create`, `mkdir`, `unlink`, `rename`,
  `symlink`, `readlink`, `chmod`, `chown`, `truncate`, `utimens`. There is
  no inode or dentry cache; every call carries the path.
- An open file is a `struct vfs_file` (path + position) in the process
  fd table; `read(2)` goes `vfs_read()` → `ops->read(path, …, pos)`.

### Pseudo filesystems and side tables (outside the mount table)

sysfs and heartfs are mounts. The syscall layer still checks the path
string *before* the VFS for what remains:

| Entry point | Order of checks |
|---|---|
| `ir0_stat_path_routed()` (`fs/path_routed.c`) | `is_proc_path` → `proc_stat`; `ir0_is_dev_path` → `devfs_stat_path`; then `named_socket_stat`, `named_fifo_stat`, `named_devnode_stat`, `named_symlink_stat`; finally `vfs_stat` (`/sys` is the sysfs mount) |
| `ir0_getdents_path_routed()` | `/dev` → `devfs_readdir_root`/`_subdir`; `/proc` → `proc_readdir`; else `vfs_readdir` |
| `sys_open_routed_resolved()` (`kernel/syscalls/fs_syscalls.c`) | `/proc`: directories → `pseudo_bind_dir_fd`, files → `pseudo_bind_file_fd` (an `is_pseudo` fd holding registry ops/ctx); `/dev` → `devfs_find_node` + `devfs_open_node` + `devfs_bind_fd_slot` (an `is_devfs` fd); else `sys_open_vfs_resolved`, which itself first checks `named_fifo_lookup`, `posix_shm_path_is` and `named_devnode_lookup` |
| `getdents_trust_readdir_type()` (`fs_dirent.c`) | trusts `d_type` for `/proc` and `/dev` virtual directories |
| `vfs_statfs()` | prefix table for `/proc` and `/dev` (Linux magic, zero blocks); `/sys` uses the sysfs `statfs` |
| `proc_mounts_read()` | real mounts (including sysfs), then fixed lines for proc and devtmpfs |

- **Registry** (`fs/pseudo_fs_registry.c`): one fixed table (`/proc`) of
  `{full_path, pseudo_fs_ops_t *, ctx}` plus the `/proc/<pid>/…` dynamic
  matcher. Nodes are registered lazily by `pseudo_fs_nodes_register_all()`
  (`fs/pseudo_fs_nodes.c`). `pseudo_fs_ops_t` reads regenerate the whole
  text into a 4 KiB bounce buffer and slice it at the offset; writes have no
  offset.
- **procfs** (`fs/procfs*.c`, ~3100 lines): `proc_stat`, `proc_readdir`
  (pid directories), `proc_access_path` (ptrace-like check on `environ`,
  `maps`, `fd`, `exe`) and the registry nodes. A `proc` fstype exists only
  so `mount -t proc proc /proc` succeeds; the mount entry it adds has no
  ops,   and the routing above answers `/proc` paths before the VFS.
- **devfs** (`fs/devfs*.c`, ~4400 lines): its own node table
  (`devfs_node_t`, by name and by rdev) with per-device ops, permissions and
  ownership (`devfs_chown_path`), PTY master/slave pairing, input, fb,
  block and net nodes; fds are `is_devfs` slots, not `vfs_file`.
- **named_\*** (`fs/named_{fifo,socket,devnode,symlink}.c`): global
  path-keyed tables for nodes created by `mknod(2)` (FIFO, device),
  `bind(2)` (AF_UNIX) and `symlink(2)` on backends without a `symlink` op.
  They shadow whatever the mounted filesystem has at that path.

### sysfs (migrated)

`/sys` is a `sysfs` `vfs_fstype` (`fs/sysfs/sysfs.c`, readers in
`fs/sysfs.c`) mounted by `boot_rootfs_init()` after heartfs. The node
table, cpu nodes and net lookup live in sysfs. `stat` synthesizes
intermediate directories; an exact file wins even when it has children
(open of that path is `EISDIR` without `O_DIRECTORY`, `ENOTDIR` with it).
Reads slice a 4 KiB regeneration at the offset; writes ignore the offset.

### heartfs (migrated)

`/heart` is a `heartfs` `vfs_fstype` mounted by `boot_rootfs_init()` after
the root is final (after the arm64 boot pivot). Its `vfs_ops` serve
`README`, `MANIFESTO`, `kernel/{version,build,features}`, `src/…` (the
build-time blob) and the aliases `proc/…` (registry, with `proc_access_path`
in the `open` hook) and `sys/…` (sysfs exact-node stat/read/write/open;
`readdir` of `/heart/sys` is sysfs readdir). `/heart/dennis` stays on the filesystem that held it, as an
internal bind mount (`vfs_bind_mount`) created when heartfs is mounted.

## 2. What each pseudo filesystem needs to be a `vfs_fstype`

All four: an fstype with `FS_NOSUID_DEFAULT`, a `mount` that accepts only
its canonical path (backend paths are absolute), `umount` → `-EBUSY`,
`statfs` with the Linux magic and zero blocks, and `vfs_ops.stat`,
`.readdir`, `.read`, `.open`. Removing it from `path_routed.c`,
`fs_syscalls.c`, `fs_dirent.c`, `vfs_statfs()` and `proc_mounts_read()`
follows.

| fs | Extra ops / VFS support needed |
|---|---|
| heartfs | none (read-only, done) |
| sysfs | done: `.write` ignores the offset; cpu/net lookup is inside sysfs |
| proc | `.readlink` for `self`, `fd/N`, `exe`; pid directories in `readdir`/`stat`; `.open` for `proc_access_path`; `.write` for the few writable nodes; per-open snapshot if a reader must see one generation across reads |
| devfs | per-node file ops beyond read/write: `ioctl`, `poll`, `mmap` (fb), PTY pairing on open/close, `chown`/`chmod` on nodes. The VFS needs an fd type whose ops come from the filesystem (Linux `file->f_op`) instead of `is_devfs` special cases in every syscall |
| named_\* | `vfs_ops.mknod` (FIFO, device, socket node) and `symlink` on every backend that lacks them (tmpfs has `symlink`; MINIX/FAT do not), or a generic in-memory overlay the VFS owns; `open` of a FIFO/device node must dispatch to the pipe/devfs code by node type, not by table lookup |

## 3. Migration steps, smallest first

1. **heartfs** — fstype + mount at boot, `vfs_ops.open` hook, internal bind
   mount for `/heart/dennis`; drop its registry table and routes. *(done)*
2. **sysfs** — move `/sys` nodes out of the registry into a sysfs-owned
   table with `vfs_ops.write`; mount `/sys`; drop the `/sys` routes,
   `sysfs_is_virtual_subdir` callers and the `/sys` registry table. *(done)*
3. **proc** — give the `proc` fstype real ops (stat/readdir/read/readlink/
   open/write) and an actual mount at `/proc`; move the `/proc` registry
   nodes and the pid matcher into procfs; then `fs/pseudo_fs_registry.c`
   and `pseudo_fd_bind` have no users and go away (the `is_pseudo` fd
   type with them).
4. **Filesystem-owned file ops** — let a `vfs_file` carry ops from the
   filesystem (`ioctl`, `poll`, `mmap`, `release`) so devices do not need
   their own fd type.
5. **devfs** — devtmpfs-style fstype on top of step 4, mounted at `/dev`;
   drop `ir0_is_dev_path` routing and `is_devfs` special cases.
6. **named_\*** — `mknod`/socket/symlink nodes stored by the backend that
   owns the parent directory (or one VFS-owned overlay); delete
   `fs/named_*.c`.
7. **Delete** `fs/path_routed.c`: `ir0_*_path_routed()` become `vfs_*()`.
