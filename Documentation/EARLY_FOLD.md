# Staged `early_*` fold into ordinary subsystems

> **Last verified:** 2026-10-16
> **Source of truth:** `fs/ramtab.c`, `includes/ir0/ramtab.h`,
> `includes/ir0/product_path.h`, `kernel/syscalls/early_rootfs.c`,
> `kernel/syscalls/early_process.c`, `arch/arm64/sources/all_objs_mark.c`,
> `make smoke-arm64-all`

Spanish: [`esp/EARLY_FOLD.md`](esp/EARLY_FOLD.md).
Manifest of remaining ARM scaffolding: [`debts/arm64-core-convergence.md`](debts/arm64-core-convergence.md).

IR0 must not grow a second kernel inside `kernel/syscalls/early_*.c`. Those
files are **adapters** for staged EL0 on ARM while product objects (`vfs.c`,
`process_*`, `syscall_dispatch.c`) take the path. x86 production never uses
this adapter as the product root.

## Layers (2026-10-16)

| Layer | Role | Path |
|-------|------|------|
| Product VFS | Mounts, `vfs_open`/`read`/`write`, tmpfs | `fs/vfs.c`, `fs/tmpfs.c` |
| Ramtab | Bounded in-memory names **not** on a VFS mount | `fs/ramtab.c` |
| Product path bind | Optional ops so the adapter can call product VFS | `includes/ir0/product_path.h` |
| Early adapter | `/`, `/init`, `/dev/*`, pipes, unix, CPIO blob | `kernel/syscalls/early_rootfs.c` |
| Early process | Staged clone/wait/identity (facades already bound) | `kernel/syscalls/early_process.c` |
| Early SVC | Semantic-ID dispatch until product table owns EL0 | `kernel/syscalls/early_dispatch.c` |

`ramtab` is **not** a `vfs_fstype`. It is the leftover name table until a
mount owns the path. Do not add new Linux ABI only inside `early_rootfs.c`.

## `/tmp` on ARM ALL

When `tmpfs_register()` + `vfs_mount("none", "/tmp", "tmpfs")` succeed, ARM
ALL binds `ir0_product_path_ops` to `vfs_*`. Then staged `openat`/`read`/
`write`/`mkdirat`/`lseek` for paths under `/tmp/` use product VFS.

| Image | `/tmp` path |
|-------|-------------|
| `kernel-arm64-boot.bin` | Ramtab / synthetic `/tmp` only (no product `vfs.c`) |
| `kernel-arm64-all.bin` | Product tmpfs + `ARM64_PRODUCT_VFS_OK` / `ARM64_EARLY_PATH_FOLD_OK` |
| `kernel-x64.bin` | Product VFS as always; ramtab is linked but unused as root |

Host ABI tests (`test_early_rootfs_abi`) still check synthetic `/`, `/init`,
and `/dev/*` nodes from `includes/ir0/early_rootfs.h`.

## Still early (do not mark done)

- Special char/block nodes and the `/init` blob
- In-adapter pipes, eventfd, memfd, timerfd, epoll, AF_UNIX
- `early_process.c` (~2k lines) — identity/exit facades exist; body is staged
- ARM EL0 syscall body is still `early_dispatch`, not `syscall_dispatch.c`
- ISD has no `rootfs/arch/aarch64` product pack

## Next fold order

1. Anon fd objects (pipe/eventfd/…) → existing `kernel/lib/pipe.c` / product fds
2. `early_process.c` → `kernel/process/` + `process_syscalls.c`
3. Bounded ARM SVC slice through `syscall_dispatch.c`
4. Common `elf_loader.c` + ISD AArch64
5. Private-mm fork / COW; then shrink `boot_stub.c`

## Gates

```bash
make -s kernel-x64.bin
make -s arch-guard
make -s -C tests/host run
make -s build-matrix-min
make -s smoke-arm64-all   # greps ARM64_EARLY_PATH_FOLD_OK
```

Linux references used for the product side: VFS
[filesystems/vfs.html](https://docs.kernel.org/filesystems/vfs.html);
`vfs_open` / `vfs_read` in Linux `fs/open.c` and `fs/read_write.c`.
