# Fold de `early_*` hacia subsistemas ordinarios

> **Última verificación:** 2026-10-16
> **Fuente de verdad:** `fs/ramtab.c`, `includes/ir0/ramtab.h`,
> `includes/ir0/product_path.h`, `kernel/syscalls/early_rootfs.c`,
> `make smoke-arm64-all`

Canónico en inglés: [`../EARLY_FOLD.md`](../EARLY_FOLD.md).

`early_*.c` es **adaptador** de EL0 en ARM, no un segundo kernel. x86 de
producto no usa este adaptador como raíz.

## Capas (2026-10-16)

| Capa | Rol |
|------|-----|
| VFS de producto (`fs/vfs.c`, tmpfs) | Montajes y I/O de path |
| `fs/ramtab.c` | Nombres en RAM **sin** montaje VFS |
| `product_path.h` | Bind opcional adaptador → `vfs_*` |
| `early_rootfs.c` | `/`, `/init`, `/dev/*`, pipes, unix, blob CPIO |
| `early_process.c` | clone/wait/identidad staged |
| `early_dispatch.c` | SVC semántico hasta `syscall_dispatch.c` |

`ramtab` **no** es un `vfs_fstype`. No añadir ABI Linux solo en early.

## `/tmp` en ARM ALL

Con tmpfs montado, `open`/`read`/`write`/`mkdir`/`lseek` bajo `/tmp/` van a
`vfs.c`. El boot mínimo (`kernel-arm64-boot.bin`) no enlaza producto VFS.
Prueba: `ARM64_PRODUCT_VFS_OK` y `ARM64_EARLY_PATH_FOLD_OK`.

## Pendiente

Pipes/anonfd, fold de `early_process.c`, slice de dispatch en ARM, ISD
AArch64, fork con mm privado.
