# Acoplamiento IR0 (kernel) ↔ ISD

> **Última verificación:** 2026-07-28  
> **Fuente de verdad:** este archivo, `scripts/make/isd.mk`, hermano [ISD](https://github.com/IRodriguez13/ISD), [SETUP.md](../../SETUP.md).  
> **English:** [`../USERSPACE.md`](../USERSPACE.md)

## Por qué dos repositorios

El kernel no es una distro. PID1 (**runit**), **BusyBox**, paquetes, rootfs y
`disk.img` viven en **ISD**. IR0 compila el kernel, exporta UAPI, delega la
build de ISD y arranca la imagen.

```bash
git clone https://github.com/IRodriguez13/IR0.git
cd IR0
make first-boot PROFILE=minimal
make run PROFILE=minimal
```

`first-boot` pregunta antes de instalar deps del host (`IR0_DEPS_INSTALL=ask|yes|never`).
Clona `../ISD` si falta. No inyecta binarios uno a uno.

| Variable | Default |
|----------|---------|
| `IR0_ISD_ROOT` | `../ISD` |
| `PROFILE` | `minimal` / `development` / … |
| `IR0_USERSPACE_*` | alias deprecado de `IR0_ISD_*` |

| Target | Rol |
|--------|-----|
| `make first-boot PROFILE=…` | Bootstrap completo |
| `make isdconfig PROFILE=…` | Extras interactivos (paquetes + applets, p. ej. top) |
| `make isd-image PROFILE=…` | Solo imagen ISD |
| `make run PROFILE=…` | QEMU con disco ISD |
| `bootstrap-userspace` | Deprecado → `first-boot` |

Config: `IR0/.config` (kernel) ≠ `ISD/.isdconfig` (extras de distro).
