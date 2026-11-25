# VFS inodes and dentries

> **Status:** stage A done; stage B part 1 done (super_block, every
> file-storing filesystem by inode); dentries and stages C-D open
> **Linux model:** `fs/inode.c` (`iget_locked`, `iput`, `evict`),
> `fs/dcache.c`, `fs/namei.c`, `mm/filemap.c` (`address_space`)

The IR0 VFS was keyed by path: an open file kept a path string and every
read went back through the name. A name is not a file, so a file that lost
its name (unlink, rename over it) lost its data for every open fd, hard
links had one page cache per name, and special files lived in side tables
keyed by path. This note fixes the object model and the order in which the
VFS moves to it.

## Object model

| Object | Owner of | Identity | Lifetime |
|--------|----------|----------|----------|
| `struct inode` | one file of one filesystem | (`i_fs`, `i_ino`) | `i_count` references; evicted at the last `iput` |
| `struct dentry` | one name in one directory | (`d_parent`, `d_name`) | `d_count` references; negative dentries may be dropped any time |
| `struct vfs_file` | one open-file description | pointer | `ref_count`; holds one inode reference |
| page cache (`struct filemap`) | the pages of one inode | the inode | holds one inode reference while it exists |

- `i_sb` is the mount's `struct super_block` (bind mounts share it);
  `s_fs_info` is the backend's state for that mount, set by
  `vfs_fstype.fill_super`. A super_block with any cached inode is busy:
  umount fails with `EBUSY`, as in Linux.
- The backend stays authoritative for metadata (`mode`, `nlink`, `uid`,
  `gid`, `size`, times): `struct inode` caches only what cannot change
  (`i_ino`, the file type). Caching the rest is a later optimization, not a
  correctness need.
- A backend that can address its files by number fills
  `vfs_ops.iops` (`struct vfs_inode_ops`: `stat`, `read`, `write`,
  `truncate`, `evict`). Its path operations become lookup plus the inode
  operation, one implementation each.

## Reference and lifetime rules

1. `iget(fs, ino)` returns the cached inode with one more reference, or a
   new one. `iput()` drops a reference.
2. `vfs_open()` resolves the name and takes the inode reference inside one
   fs-op critical section, so no unlink can free the file in between.
3. Unlink and rename-over drop a name. When the backend's link count
   reaches 0 it calls `inode_orphan(fs, ino)`: if the VFS holds the inode,
   the inode is marked orphan and the backend keeps its blocks; otherwise
   the backend frees them at once, as before.
4. The last `iput()` of an orphan calls `iops->evict(ino)`, under the
   fs-op lock, which frees the on-disk inode and its blocks (Linux
   `evict()` with `i_nlink == 0`). No orphan list is written to disk: a
   crash with an open unlinked file leaks the inode until fsck, as on
   ext2 without the orphan feature.
5. The page cache of an inode holds a reference, so a mapped file outlives
   its last name and its last close; reclaim of an unmapped cache drops it.

## Locking (SMP-ready, one owner per structure)

| Structure | Guard |
|-----------|-------|
| inode hash, `i_count`, `i_state` | `icache_lock` (spinlock, IRQ-safe, never held across I/O) |
| backend data reached through an inode | the fs-op lock (`vfs_fs_op_acquire`) |
| dentry hash and `d_count` (stage B) | `dcache_lock` |
| page cache list and frames | `filemap_lock`, plus the fs-op lock across backend I/O |

Order: fs-op lock, then `icache_lock` or `filemap_lock`; the two spinlocks
are never nested. `evict` runs with only the fs-op lock held. The new
globals are rows of `scripts/smp_entry_gate/global_state.tsv` (SMP-0).

## Migration

- **A (inodes, done):** `struct inode` and the inode cache; an open file
  pins its inode; MINIX and tmpfs address files by inode and keep orphans
  until the last close; the page cache belongs to the inode, so hard links
  share it. Proof: `fs_unlink_open_file`, `fs_rename_over_open_file`,
  `fs_link_shared_mapping`, `fs_tmpfs_inode_lifetime`. tmpfs numbers its
  inodes uniquely across mounts; backends without `iops` keep a
  path-named inode (it follows renames) until they gain inode operations.
- **B (dentries):** `struct super_block` per mount (done; umount of a
  busy filesystem is `EBUSY`); ext2 and simplefs get `iops` (done).
  FAT16 and 9p keep per-file backend state in `i_private`
  (`iops->open_inode`/`release`): FAT16 its directory slot (st_ino is
  the slot, Linux i_pos), 9p a fid (st_ino is the folded qid.path). The
  path-named inode is gone (done): a pseudo-filesystem file has no inode
  until stage D. Then the dentry cache, namei
  walks dentries (the Q3 search-permission walk stays per component), path
  operations take (dentry, name) instead of strings.
- **C (special files):** FIFOs, sockets and device nodes are inodes made by
  `mknod`/`mkfifo`/`bind` on filesystems that store them (ext2, tmpfs);
  others return the Linux errno. The `named_*` tables go.
- **D (pseudo filesystems):** devfs becomes a `vfs_fstype` with inodes;
  `fs/path_routed.c`, the POSIX shm prefix route (to tmpfs on `/dev/shm`)
  and `pseudo_bind_dir_fd` go, with the dead `sysfs_alias_*` and
  `is_pseudo` branches.

Each step builds on both ISAs and keeps fs-abi, mm-abi and conformance
green; a behaviour change carries a red/green conformance case checked on
Linux.
