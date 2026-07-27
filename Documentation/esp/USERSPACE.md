# Acoplamiento IR0 (kernel) ↔ IR0-userspace

> **Última verificación:** 2026-07-26  
> **Fuente de verdad:** este archivo, `Makefile` (`bootstrap-userspace`, `IR0_USERSPACE_ROOT`), hermano [IR0-userspace](https://github.com/IRodriguez13/IR0-userspace), [SETUP.md](../../SETUP.md).  
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

Perfil de producto: `IR0_PRODUCT_PROFILE=minimal|development|desktop|appliance`
(por defecto `minimal`: registro en primer boot + doas). Detalle EN: [`../USERSPACE.md`](../USERSPACE.md).

En el guest:

```text
busybox
ls /
cat /proc/version
man IR0-boot
man IR0-uspace
man -w IR0-tty
```

| Target | Rol |
|--------|-----|
| `make first-boot` | Hermano + distro mínima |
| `make run` | GTK con runit+BusyBox (sin TinyCC obligatorio) |
| `IR0_WITH_DEVTOOLS=1 make run` | + toolchain in-guest (opcional) |
| `make prepare-guest-mandocs` | Páginas IR0 `cat7` (ASCII) para `man` en guest |
| `make check-guest-mandocs` | Comprueba que no sean macros mdoc crudas |

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
