# Fold de `early_*` hacia subsistemas ordinarios

> **Última verificación:** 2026-11-06
> **Fuente de verdad:** `fs/ramtab.c`, `ipc/early_anonfd.c`, `ipc/early_unix.c`,
> `includes/ir0/bpath.h`, `kernel/syscalls/erfs*.c`,
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
| `bpath.h` | Bind opcional adaptador → `vfs_*` |
| `early_rootfs.c` | `/`, `/init`, `/dev/*`, blob CPIO, routing |
| `early_process.c` | clone/wait; pid hijo desde el registry |
| Providers de producto | `getpid`/`getcwd` en ARM ALL |
| `early_dispatch.c` | SVC semántico |

## Pivot de producto (initramfs → tmpfs FHS) en ARM ALL

`ir0_bpivot_mount_trees()` monta tmpfs en `/bpivot`, `/tmp`, `/run`, `/usr`,
`/etc`, `/bin`, `/sbin`, `/var`, `/lib`, `/opt`, `/home`, `/root`, `/srv`,
`/mnt`. `ir0_bpivot_commit()` arma `/bpivot/new` y `/bpivot/old` y poda
ramtab donde el VFS ya tiene el objeto. Miembros **newc CPIO** bajo esos
árboles se pliegan a `vfs.c`;
`/dev/*`, `/` e `/init` siguen en erfs. Tags: `ARM64_PRODUCT_VFS_OK`,
`ARM64_EARLY_PATH_FOLD_OK`, `ARM64_INITRAMFS_CPIO_OK`,
`ARM64_BPIVOT_COMMIT_OK`.

El kernel de producto ARM64 (`kernel-arm64-product.bin` / `kernel-arm64.bin`,
**`$(ALL_OBJS)`**) lleva tmpfs + ramtab; no existe enlace separado
`kernel-arm64-all.bin`.

## Dispatch de producto

`kernel-arm64-product.bin` enlaza la tabla común `syscall_dispatch.c`.
SVC de EL0 entra ahí (no `arm64_syscall_early`). Los objetos de bring-up
(`syscall_early`, `hello_embed`, `rr_early`, `rootfs_early`, …) **no**
están en el link de producto.

Las etiquetas históricas de fold-slice (`ARM64_PRODUCT_*_SLICE_OK` /
binds de providers en `all_objs_mark.c`) son solo marcadores de bring-up.
Prueba de ABI de producto:

| Gate | Contrato |
|------|----------|
| `make ir0-selftest-arm64` | `openat` KTAP vía `syscall_dispatch` |
| `make ir0-selftest-identity-arm64` | `getpid` + `write(1)` |
| `make smoke-arm64-product-usermode-log` | CPIO `/sbin/init` + `ARM64_PRODUCT_EL0_SYSCALL_OK` |

PID 1 viene de CPIO/`vda` virtio-blk (o falla con honestidad). No hay seed
hello embebido en la imagen de producto.

## ISD AArch64

No hay target `make isd-aarch64-prep` en este árbol. Para solo headers:
`make headers_install` (exporta `includes/uapi` a `DESTDIR`). El empaquetado
de rootfs AArch64 sigue siendo trabajo **ISD**.

## Pendiente

transporte `virtio_blk_early` → driver de producto, rootfs ISD AArch64,
`early_process.c` residual en bring-up freestanding.
