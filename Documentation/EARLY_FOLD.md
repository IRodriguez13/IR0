# Staged `early_*` fold into ordinary subsystems

> **Last verified:** 2026-11-06
> **Source of truth:** `fs/ramtab.c`, `ipc/early_anonfd.c`, `ipc/early_unix.c`,
> `includes/ir0/product_path.h`, `kernel/syscalls/early_rootfs.c`,
> `kernel/syscalls/early_process.c`, `arch/arm64/sources/all_objs_mark.c`,
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
| Product path bind | Optional ops so the adapter can call product VFS | `includes/ir0/product_path.h` |
| Early adapter | `/`, `/init`, `/dev/*`, CPIO blob, syscall routing | `kernel/syscalls/early_rootfs.c` |
| Early process | Staged clone/wait; child pid from process registry | `kernel/syscalls/early_process.c` |
| Product identity/cwd | Semantic table providers on ARM ALL | `ir0_product_identity_provider`, `ir0_product_cwd_provider` |
| Early SVC | Semantic-ID dispatch until more product IDs move | `kernel/syscalls/early_dispatch.c` |

`ramtab` is **not** a `vfs_fstype`. Do not add new Linux ABI only inside
`early_rootfs.c`.

## `/tmp` on ARM ALL

When `tmpfs_register()` + `vfs_mount("none", "/tmp", "tmpfs")` succeed, ARM
ALL binds `ir0_product_path_ops` to `vfs_*`. Then staged `openat`/`read`/
`write`/`mkdirat`/`lseek` for paths under `/tmp/` use product VFS.

The **ARM64 product kernel** (`kernel-arm64-product.bin` / `kernel-arm64.bin`,
linked from **`$(ALL_OBJS)`**) carries tmpfs + ramtab + the fold tags below.
There is no separate `kernel-arm64-all.bin` link anymore.

| Image | `/tmp` path |
|-------|-------------|
| `kernel-arm64-product.bin` / `kernel-arm64.bin` | Product tmpfs + `ARM64_PRODUCT_VFS_OK` / `ARM64_EARLY_PATH_FOLD_OK` when bound |
| `kernel-x64.bin` | Product VFS as always; ramtab is linked but unused as root |

Host ABI tests (`test_early_rootfs_abi`) still check synthetic `/`, `/init`,
and `/dev/*` nodes from `includes/ir0/early_rootfs.h`.

## Product dispatcher slice (ARM ALL)

`syscall_dispatch.c` is **not** linked on ARM (it pulls x86-only product
surface). The honest slice is extra `syscall_context_provider` rows:

| Provider | IDs | Tag |
|----------|-----|-----|
| `ir0_product_identity_provider` | getpid/gettid/getppid/getuid/geteuid/getgid/getegid | `ARM64_PRODUCT_IDENTITY_OK` |
| `ir0_product_cwd_provider` | getcwd via `current_process` + `copy_to_user` | `ARM64_PRODUCT_CWD_OK` |
| `ir0_product_ipc_provider` | pipe2 / socketpair (`ipc/` tables + ring I/O) | `ARM64_PRODUCT_IPC_OK` |
| `ir0_product_vfs_provider` | chdir (path-checked `/tmp`), openat, newfstatat, fstat | `ARM64_PRODUCT_VFS_SLICE_OK` |

PIPE2/SOCKETPAIR are always the ipc/ product table.

## ISD AArch64

There is **no** `make isd-aarch64-prep` target in this tree. For headers only,
use `make headers_install` (exports `includes/uapi` to `DESTDIR`, default
`build/uapi-install`). Packaging an AArch64 rootfs remains **ISD** work; the
kernel does not invent one.

## Still early (do not mark done)

- Special char/block nodes and the `/init` blob
- `early_process.c` (~2k lines) — clone pid can come from the registry
- ARM EL0 body is still `early_dispatch`, not `syscall_dispatch.c`
- ISD has no `arch/aarch64` / `rootfs/arch/aarch64` product pack

## Next fold order

1. Route remaining AF_UNIX bind/connect through `ipc/` helpers
2. `early_process.c` → `kernel/process/` + `process_syscalls.c`
4. Common `elf_loader.c` + ISD AArch64 rootfs
5. Private-mm fork / COW; then shrink `boot_stub.c`

## Gates

```bash
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
