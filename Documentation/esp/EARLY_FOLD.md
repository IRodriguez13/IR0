# Fold de `early_*` hacia subsistemas ordinarios

> **Última verificación:** 2026-11-06
> **Fuente de verdad:** `fs/ramtab.c`, `ipc/early_anonfd.c`, `ipc/early_unix.c`,
> `includes/ir0/product_path.h`, `kernel/syscalls/erfs*.c`,
> `make kernel-arm64.bin` (`kernel-arm64-product.bin`)

Canónico en inglés: [`../EARLY_FOLD.md`](../EARLY_FOLD.md).

`early_*.c` es **adaptador** de EL0 en ARM, no un segundo kernel. x86 de
producto no usa este adaptador como raíz.

IPC staged vive en **`ipc/`** (junto a `ipc/ipc.c`), no en `fs/`.

## Capas (2026-10-18)

| Capa | Rol |
|------|-----|
| VFS de producto (`fs/vfs.c`, tmpfs) | Montajes y I/O de path |
| `fs/ramtab.c` | Nombres en RAM **sin** montaje VFS |
| `ipc/early_anonfd.c`, `ipc/early_unix.c` | Tablas pipe/eventfd/AF_UNIX |
| `product_path.h` | Bind opcional adaptador → `vfs_*` |
| `early_rootfs.c` | `/`, `/init`, `/dev/*`, blob CPIO, routing |
| `early_process.c` | clone/wait; pid hijo desde el registry |
| Providers de producto | `getpid`/`getcwd` en ARM ALL |
| `early_dispatch.c` | SVC semántico |

## Pivot de producto (initramfs → tmpfs FHS) en ARM ALL

`ir0_product_pivot_mount_trees()` monta tmpfs en `/tmp`, `/run`, `/usr`,
`/etc`, `/bin`, `/sbin`, `/var`, `/lib`, `/opt`, `/home`, `/root`, `/srv`,
`/mnt`. Los miembros **newc CPIO** bajo esos árboles se pliegan a `vfs.c`;
`/dev/*`, `/` e `/init` siguen en erfs. Tags: `ARM64_PRODUCT_VFS_OK`,
`ARM64_EARLY_PATH_FOLD_OK`, `ARM64_INITRAMFS_CPIO_OK`,
`ARM64_EARLY_ROOT_PIVOT_OK`.

El kernel de producto ARM64 (`kernel-arm64-product.bin` / `kernel-arm64.bin`,
**`$(ALL_OBJS)`**) lleva tmpfs + ramtab; no existe enlace separado
`kernel-arm64-all.bin`.

## Dispatch de producto

No se enlaza `syscall_dispatch.c` en ARM. Slice honesto: providers
`ir0_product_identity_provider`, `ir0_product_cwd_provider`,
`ir0_product_ipc_provider` y `ir0_product_vfs_provider`
(`ARM64_PRODUCT_IDENTITY_OK`, `ARM64_PRODUCT_CWD_OK`,
`ARM64_PRODUCT_IPC_OK`, `ARM64_PRODUCT_VFS_SLICE_OK`).

## ISD AArch64

No hay target `make isd-aarch64-prep` en este árbol. Para solo headers:
`make headers_install` (exporta `includes/uapi` a `DESTDIR`). El empaquetado
de rootfs AArch64 sigue siendo trabajo **ISD**.

## Pendiente

bind/connect AF_UNIX restantes, `early_process.c`, rootfs ISD AArch64,
fork con mm privado.
