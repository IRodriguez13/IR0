# Acoplamiento IR0 (kernel) ↔ IR0-userspace

> **Última verificación:** 2026-07-26  
> **Fuente de verdad:** este archivo, `Makefile` (`bootstrap-userspace`, `IR0_USERSPACE_ROOT`), hermano [IR0-userspace](https://github.com/IRodriguez13/IR0-userspace), [SETUP.md](../../SETUP.md).  
> **Inglés:** [`../USERSPACE.md`](../USERSPACE.md)

## ¿Por qué dos repos?

El kernel solo no es una distro Unix. El PID1 de producto (**runit**), **BusyBox**, login/doas y `/etc` viven en **IR0-userspace**. El boot siempre hace `kexecve("/sbin/init")` desde `disk.img`.

## Camino más corto (primera vez)

```bash
git clone https://github.com/IRodriguez13/IR0.git
cd IR0
make check-env
make defconfig
make first-boot    # clona ../IR0-userspace si falta + rootfs mínimo + ISO
make run           # QEMU → getty → BusyBox ash
```

En el guest:

```text
busybox
ls /
cat /proc/version
```

| Target | Rol |
|--------|-----|
| `make first-boot` | Hermano + distro mínima |
| `make run` | GTK con runit+BusyBox (sin TinyCC obligatorio) |
| `IR0_WITH_DEVTOOLS=1 make run` | + toolchain in-guest (opcional) |

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
