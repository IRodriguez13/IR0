# Staged `early_*` fold into ordinary subsystems

> **Last verified:** 2026-11-07
> **Source of truth:** `fs/ramtab.c`, `ipc/early_anonfd.c`, `ipc/early_unix.c`,
> `includes/ir0/bpath.h`, `kernel/syscalls/erfs*.c`,
> `kernel/syscalls/early_process.c`,
> `make kernel-arm64.bin` (product link: `kernel-arm64-product.bin`)

Spanish: [`esp/EARLY_FOLD.md`](esp/EARLY_FOLD.md).
Manifest of remaining ARM scaffolding: [`debts/arm64-core-convergence.md`](debts/arm64-core-convergence.md).

IR0 must not grow a second kernel inside `kernel/syscalls/early_*.c`. Those
files are **adapters** for staged EL0 on ARM while product objects (`vfs.c`,
`process_*`, identity/cwd providers) take the path. x86 production never uses
this adapter as the product root.

IPC objects belong under **`ipc/`** at the repository root (with `ipc/ipc.c`).
Do not park pipe/unix/eventfd tables in `fs/`.

## Layers (2026-10-18)

| Layer | Role | Path |
|-------|------|------|
| Product VFS | Mounts, `vfs_open`/`read`/`write`, tmpfs | `fs/vfs.c`, `fs/tmpfs.c` |
| Ramtab | Bounded in-memory names **not** on a VFS mount | `fs/ramtab.c` |
| Staged IPC tables | pipe/eventfd/memfd/timerfd/epoll, AF_UNIX/socketpair | `ipc/early_anonfd.c`, `ipc/early_unix.c` |
| Product path bind | Optional ops so the adapter can call product VFS | `includes/ir0/bpath.h` |
| Early adapter (`erfs`) | `/`, `/init`, `/dev/*`, CPIO blob, syscall routing | `kernel/syscalls/erfs_*.c` |
| Early process | Staged clone/wait; child pid from process registry | `kernel/syscalls/early_process.c` |
| Product identity/cwd | Semantic table providers on ARM ALL | `ir0_product_identity_provider`, `ir0_product_cwd_provider` |
| Early SVC | Semantic-ID dispatch until more product IDs move | `kernel/syscalls/early_dispatch.c` |

`ramtab` is **not** a `vfs_fstype`. Do not add new Linux ABI only inside
`early_rootfs.c`.

## Product pivot (initramfs → tmpfs trees) on ARM ALL

`ir0_bpivot_mount_trees()` (`fs/bpivot.c`) mounts **tmpfs** on
`/bpivot`, `/tmp`, `/run`, `/usr`, `/etc`, `/bin`, `/sbin`, `/var`, `/lib`,
`/opt`, `/home`, `/root`, `/srv`, `/mnt`. `ir0_bpivot_commit()` creates
`IR0_BPIVOT_NEW_ROOT` / `IR0_BPIVOT_PUT_OLD` staging and prunes ramtab
entries that now exist on product VFS. ARM ALL binds `ir0_bpath_ops` to
`vfs_*`. Staged `openat`/`read`/`write`/`mkdirat`/`lseek` and **newc CPIO**
members under those trees use product VFS (`ir0_bpath_is_foldable` in
`fs/bpath.c`). **`/dev/*`**, **`/`**, and **`/init`** stay on erfs
(`ir0_bpath_is_reserved_early`). After `early_rootfs_cpio_bpath_fold()`:
Portable smoke tags: `IR0_BPIVOT_MOUNT_OK`, `IR0_BPATH_FOLD_OK`,
`IR0_INITRAMFS_CPIO_OK` (when CPIO fold runs), `IR0_BPIVOT_STAGING_OK`,
`IR0_BPIVOT_COMMIT_OK`. ARM64 product boot also emits `ARM64_BPIVOT_COMMIT_OK`.
x86 exercises the same path via ktest `bpivot_boot_contract`.

The **ARM64 product kernel** (`kernel-arm64-product.bin` / `kernel-arm64.bin`,
linked from **`$(ALL_OBJS)`**) carries tmpfs + ramtab + the fold tags below.
There is no separate `kernel-arm64-all.bin` link anymore.

| Image | `/tmp` path |
|-------|-------------|
| `kernel-arm64-product.bin` / `kernel-arm64.bin` | Product tmpfs + `ARM64_PRODUCT_VFS_OK` / `ARM64_EARLY_PATH_FOLD_OK` when bound |
| `kernel-x64.bin` | Product VFS as always; ramtab is linked but unused as root |

Host ABI tests (`test_early_rootfs_abi`) still check synthetic `/`, `/init`,
and `/dev/*` nodes from `includes/ir0/early_rootfs.h`.

## Product SVC path (ARM ALL)

`kernel-arm64-product.bin` links the common `syscall_dispatch.c` table.
EL0 SVC enters that table. The freestanding bring-up objects
(`syscall_early`, `hello_embed`, `rr_early`, `rootfs_early`, `all_objs_mark`,
…) were deleted on 2026-11-07; nothing linked them.

Historical fold-slice tags (`ARM64_PRODUCT_*_SLICE_OK`) were bring-up
markers only. Product ABI proof is:

| Gate | Contract |
|------|----------|
| `make ir0-selftest-arm64` | `openat` KTAP via `syscall_dispatch` |
| `make ir0-selftest-identity-arm64` | `getpid` + `write(1)` |
| `make smoke-arm64-product-usermode-log` | CPIO `/sbin/init` + `ARM64_PRODUCT_EL0_SYSCALL_OK` |

PID 1 comes from virtio-blk CPIO/`vda` (or fails honestly). There is **no**
embedded hello seed on the product image.

## ISD AArch64

There is **no** `make isd-aarch64-prep` target in this tree. For headers only,
use `make headers_install` (exports `includes/uapi` to `DESTDIR`, default
`build/uapi-install`). Packaging an AArch64 rootfs remains **ISD** work; the
kernel does not invent one.

## Still early (do not mark done)

- Special char/block nodes and the `/init` blob on the staged adapter
- `early_process.c` (~2k lines) — clone pid can come from the registry
- virtio-blk still uses the `virtio_blk_early` transport (product needs `vda`)
- ISD has no `arch/aarch64` / `rootfs/arch/aarch64` product pack

## Next fold order

1. Route remaining AF_UNIX bind/connect through `ipc/` helpers
2. `early_process.c` → `kernel/process/` + `process_syscalls.c`
4. Common `elf_loader.c` + ISD AArch64 rootfs
5. Private-mm fork / COW on ARM (`ir0-selftest-fault-arm64`)

## Gates

```bash
make -s wave-a-gates         # x64 T1 slice: host + pipe/wait4/execve/lifecycle/pty audits
make -s kernel-x64.bin
make -s arch-guard
make -s -C tests/host run
make -s kernel-arm64.bin   # arm64-common-core-compile + arm64-provider-audit
make -s smoke-arm64-hypervisor-log   # optional; needs qemu-system-aarch64
```

Linux references: VFS
[filesystems/vfs.html](https://docs.kernel.org/filesystems/vfs.html);
getcwd(2) [man7](https://man7.org/linux/man-pages/man2/getcwd.2.html);
getpid(2) [man7](https://man7.org/linux/man-pages/man2/getpid.2.html).
