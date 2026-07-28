# Acoplamiento IR0 (kernel) ↔ IR0-userspace

> **Última verificación:** 2026-07-28  
> **Fuente de verdad:** este archivo, `Makefile` (`bootstrap-userspace`, `IR0_USERSPACE_ROOT`), hermano [IR0-userspace](https://github.com/IRodriguez13/IR0-userspace), [SETUP.md](../../SETUP.md), [`../testing/BUSYBOX_MATRIX.md`](../testing/BUSYBOX_MATRIX.md), [`../testing/DOOM_FASE55D.md`](../testing/DOOM_FASE55D.md).  
> **Inglés:** [`../USERSPACE.md`](../USERSPACE.md)

## ¿Por qué dos repos?

El kernel solo no es una distro Unix. El PID1 de producto (**runit**), **BusyBox**, login/doas y `/etc` viven en **IR0-userspace**. El boot siempre hace `kexecve("/sbin/init")` desde `disk.img`.

## Camino más corto (primera vez)

Clonar ambos como hermanos (nombres por defecto):

```bash
git clone https://github.com/IRodriguez13/IR0.git
git clone https://github.com/IRodriguez13/IR0-userspace.git
cd IR0
make check-env
make defconfig
make first-boot    # UAPI + rootfs + ISO (o clona el hermano si falta)
make run           # QEMU → getty → BusyBox ash
```

Perfil de producto: `IR0_PRODUCT_PROFILE=minimal|development|desktop|appliance`.
`make run` / `load-userspace-devtools` usan **`minimal`** (wizard «Create your account»), no `development`.
Lab: `IR0_PRODUCT_PROFILE=development make load-userspace-devtools`. Detalle EN: [`../USERSPACE.md`](../USERSPACE.md).

En el guest (tras firstboot):

```text
busybox --list          # ~380 applets
ls --help
df
mount
man IR0-boot
```

| Target | Rol |
|--------|-----|
| `make first-boot` | Hermano + distro mínima |
| `make run` | GTK: **minimal + firstboot**; TinyCC/make ON por defecto |
| `IR0_WITH_DEVTOOLS=0 make run` | Sin inyectar toolchain |
| `make busybox-matrix` | Gates de applets + flags → `bb_status.tsv` |
| `make prepare-guest-mandocs` | Páginas IR0 `cat7` (ASCII) para `man` en guest |
| `make check-guest-mandocs` | Comprueba que no sean macros mdoc crudas |

### BusyBox casi-full (BUSY-3)

- Binario producto ~381 applets; `--help` / long opts / fancy ON.
- Kernel: stack userspace 512 KiB; `sys_statfs` / `sys_fstatfs` para que `df` no cuelgue en MINIX.
- `sys_mount` 5-arg Linux; `MS_REMOUNT|MS_RDONLY` OK; bind aún no; remount string legacy para recovery.
- `/proc/mounts` muestra `ro`/`rw`; `/etc/mtab` → `/proc/mounts`.
- PMM **[32 MiB, 512 MiB)**; `USER_MMAP_START` a **512 MiB** (`0x20000000`); hints mmap solo en el arena.
- Matrix: drenaje hasta **EOF** tras `waitpid` (no cortar en `EAGAIN`); matcher streaming; protocolo `BBCASE_*` / `BBMATRIX_END`; sin mmap post-fork; **sin** `poll(fd, timeout>0)` en el padre (pool `poll_waiter` de IR0). Ver [`../testing/BUSYBOX_MATRIX.md`](../testing/BUSYBOX_MATRIX.md).

### Doom T2 (FASE55D) — mouse + PCM

Backend `setup/doom/doomgeneric_ir0.c`: `/dev/fb0`, `/dev/events0`, `/dev/audio`. Mouse `EV_REL`+botones; sonido 11025 Hz 8-bit mono sin `-nosound`; música sigue `-nomusic`.

```bash
IR0_LEGACY_SMOKE=1 make smoke-fase55d-doomgeneric REAL_WAD_PATH=/ruta/doom1.wad
```

Tags obligatorios: `DOOMGENERIC_MOUSE_CAPS_OK`, `DOOMGENERIC_AUDIO_OK`, `DOOMGENERIC_AUDIO_WRITE_OK`, frames + `FASE55D_DOOMGENERIC_OK` / `KTM_DOOM_55D_OK`. QEMU con SB16. Detalle EN: [`../testing/DOOM_FASE55D.md`](../testing/DOOM_FASE55D.md).

### TinyCC en guest (`libtcc1.a`)

`make build-tcc-fase52` usa `--tccdir=/lib/tcc`. `inject_devtools_minix.sh` exige `libtcc1.a` / `libc.a` / `crt*` en el disco.

### ESC en TTY (BusyBox vi)

Scancode PS/2 `0x01` → ASCII `0x1b` en `ps2_set1` (host test en `tests/host`).

## Manuales en guest (`man`) — Implementado

BusyBox `man` + páginas ASCII en `/usr/share/man/cat7/` (sin `nroff`/`mandoc` en el guest). El host las genera con `mandoc -Tascii` (`make prepare-guest-mandocs`); `load-userspace-runit` / `first-boot` las inyectan por defecto (`IR0_GUEST_MANDOCS=0` para omitir).

MINIX v1 limita nombres a 14 caracteres: en guest usá `man IR0-uspace` y `man IR0-onboard` (contenido de IR0-userspace / IR0-onboarding).

| Contrato | Estado |
|----------|--------|
| `man IR0-boot` (texto legible) | **Implementado** |
| Subset (7 páginas; alias cortos donde hace falta) | **Implementado** |
| Catálogo completo / ES en guest | **Parcial** — en host: `make sync-mandocs` |
| Páginas Linux genéricas (`man ls`) | Fuera de alcance |

## Contrato de boot: sin init / init muere

| Situación | Comportamiento |
|-----------|----------------|
| No hay `/sbin/init` o falla `kexecve` | **`panic("Failed to load /sbin/init")`** — no hay shell in-kernel |
| Init corre | Camino normal (runit → getty → ash) |
| Init **sale** | Reparent; el idle del kernel sigue — no hay panic automático; el userspace queda muerto |
| No se inyectó el rootfs del hermano | Mismo panic de la primera fila |

## Layout

```text
parent/
├── IR0/
├── IR0-userspace/
└── IR0-desktop/     # opcional
```

Docs completas en inglés: [`../USERSPACE.md`](../USERSPACE.md).
