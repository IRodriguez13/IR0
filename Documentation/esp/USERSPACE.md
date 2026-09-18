# Acoplamiento IR0 (kernel) ↔ ISD

> **Última verificación:** 2026-09-18  
> **Fuente de verdad:** este archivo, `scripts/make/isd.mk`, hermano [ISD](https://github.com/IRodriguez13/ISD), [SETUP.md](../../SETUP.md),
> `ISD/services/runit_console_run.c`.  
> **English:** [`../USERSPACE.md`](../USERSPACE.md)

## Por qué dos repositorios

El kernel no es una distro. PID1 (**runit**), **BusyBox**, paquetes, rootfs y
`disk.img` viven en **ISD**. IR0 compila el kernel, exporta UAPI, delega la
build de ISD y arranca la imagen.

## Camino feliz (primera vez)

Host **x86_64 Linux** (o WSL2). Objetivo: clonar → dos comandos → QEMU.

```bash
git clone https://github.com/IRodriguez13/IR0.git
cd IR0
make first-boot PROFILE=minimal
make run PROFILE=minimal
```

| PROFILE | En el guest |
|---------|-------------|
| `minimal` | Firstboot interactivo (crear usuario) + login |
| `development` | Autologin root (lab) |
| `desktop` | Como minimal + más applets (pack más lento) |

`first-boot` pregunta antes de instalar deps del host (`IR0_DEPS_INSTALL=ask|yes|never`).
Clona `../ISD` si falta. `make run` puede tardar 1–3 min en el primer pack
MINIX (imprime progreso); no está colgado tras el ISO.

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
| `make run-console PROFILE=…` | Sin GTK |
| `bootstrap-userspace` | Deprecado → `first-boot` |

### Login / getty (2026-09-02)

El spam `LOGIN_USER_READ` post-firstboot era un busy-loop de getty (`EINTR`/`EOF`
+ tag en cada iteración). Fix en ISD: `ir0_read_line` reintenta `EINTR`; el tag
solo si el username no está vacío. Detalle: [`../USERSPACE.md`](../USERSPACE.md).

`chown root` con `0755` sigue permitiendo ejecutar a otros (Unix). `/tmp` tmpfs
es intencional.

Config: `IR0/.config` (kernel) ≠ `ISD/.isdconfig` (extras de distro).

## Máquina persistente + kmang

Los ISO de kernel de una máquina instalada viven en el hermano `IR0-machines/`,
gestionados por `scripts/kernel_manager.py` (`make kmang`).

| Concepto | Significado |
|----------|-------------|
| Workspace | `IR0/kernel-x64-userspace.iso` recién construido |
| Default | Symlink `kernel-current.iso` — lo que arranca `make poweron` |
| Fallback | Default anterior tras un select/install |
| Installed | Copias en catálogo con SHA-256 |

**Los números de build son locales a la máquina.** `.build_number` sube en cada
link de ese host. `#1420` en un smoke y `#1450` en tu PC pueden ser el mismo
tip de rama; no uses `#N` como id global. kmang guarda procedencia (host/user,
git corto opcional, SHA-256) para ver si el Workspace diverge del Default.

Tras cambiar el kernel: reconstruir el ISO → `make kmang` → `i` → `b`
(o `make kernel-manager-install && make poweron`). Un ISD viejo arranca con un
kernel nuevo pero no mostrará la sesión X11 verificada hasta actualizar el
rootfs (`make isd-image PROFILE=desktop` / `machine-update-userspace`).

## Versionado de userspace (ISD, implementado)

| Capa | Mecanismo |
|------|-----------|
| Release ISD | `ISD/VERSION` → `VERSION_ID` + `/usr/share/isd/release.txt` |
| Orígenes de paquetes | `/usr/share/isd/package-manifest.txt` |
| Base userland | `USERLAND_BASE=busybox` (`coreutils` reservado) |
| Guest | `ir0-status version \| packages \| userland` |
| Host | `make usmang` |

IR0 se adapta al ABI Linux/musl; no se parchean clientes X upstream. Ver
[`../DESKTOP_ABI.md`](../DESKTOP_ABI.md).
