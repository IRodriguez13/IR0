# IR0 Userspace Bootstrap

| Campo | Valor |
|-------|-------|
| Version | 0.3 |
| Fase IR0 | T1–T2 |
| Estado | estable |
| Depende de | boot, process, vfs, tty |
| Man page | Acoplamiento IR0-ISD (sección 7) |
| Fuentes principales | `kernel/main.c`, `Makefile` raíz, `IR0_ISD_INTERFACE`, `Documentation/USERSPACE.md`, repo hermano `ISD/` |

> **Última verificación:** 2026-10-29

> **Nota de build (2026-10-29):** el Makefile de IR0 ya no clona ni compila
> ISD. El rootfs y los perfiles los construye el hermano **`ISD/`**
> (`IR0_ISD_ROOT`). Targets del kernel: `make ir0`, `make run DISK=…`,
> `make headers_install` y los gates CTR/ktest. Se eliminaron `first-boot`,
> `build-runit`, `load-userspace-runit`, `run-ash-gui`, `smoke-tier1`,
> `kmang`, `poweron` y `usmang`.

> **Nota (2026-07-24):** el PID1 transitorio **irinit** se eliminó. Producto y
> tests usan solo **runit**; el rootfs lo arma ISD.

## 1. Resumen

El boot de producción siempre carga **`/sbin/init`** vía `kexecve` desde `kmain`.
El PID1 canónico es **runit** (`ISD/out/bin/runit-init`
y stages), estático con musl —lo construye el repo hermano— e inyectado en el
rootfs MINIX. BusyBox, TCC y
DoomGeneric son payloads opcionales.

## 2. Arquitectura

| Artefacto | Rol |
|-----------|-----|
| `runit-init` + stages | PID1: stage1 → stage2 → consola/ash |
| `init_musl.c` | smoke de syscalls musl |
| `rootfs_base.c` | Crea `/bin`, `/sbin`, `/dev`, `/proc`, … |
| `inject_init_minix.py` | Escribe binarios en imagen MINIX v1 |
| `busybox-1.36.1` | Applets; receta y configs en `ISD/packages/busybox/` |
| `kernel-x64.iso` | ISO de kernel que construye este árbol |
| `disk.img` | Rootfs construido por ISD; se adjunta en runtime con `DISK=…` |

**Comportamiento runit:** stage 1 prepara el rootfs; stage 2 supervisa
servicios; la consola hace getty/login (`runit_console_run`): auth contra
`/etc/passwd`+`/etc/shadow` (`crypt(3)` musl), `setgid`/`setgroups`/`setuid`/
`chdir`, export de `HOME`/`USER`/`HOSTNAME`, y exec de ash como **login shell**
(`argv[0] = "-sh"`) para que corra `/etc/profile`. BusyBox de producto:
`CONFIG_ASH_EXPAND_PRMT` + `CONFIG_ASH_TEST`. Prompt: `# ` (root) o
`user@host:$PWD$ ` (resto). Cuentas: `root` (vacía), `ir0`/`ivan` (MD5 crypt).
Smokes: `smoke-runit-login`, `smoke-runit-login-nonroot`.

**Perfiles de producto.** `/etc/ir0-profile` (lo escribe
`ISD/scripts/install-to-disk.sh` a partir de `IR0_PRODUCT_PROFILE`)
define la política de consola: `development` mantiene autologin root con
advertencia visible, `desktop` muestra `hostname login:` y bloquea el login
directo de root con `/etc/ir0-noroot` (nombre ≤14 bytes por el límite de
entradas de directorio de MINIX v1), `appliance` no abre login interactivo
(`CONSOLE_NO_LOGIN`).

## 3. Flujo de datos

```text
  make ir0                        # kernel-x64.iso
       │
       ▼
  make run DISK=../ISD/out/.../disk.img
       │
       ▼
  QEMU boot → kexecve("/sbin/init")
       │
       ▼
  runit stage1 → stage2 → consola → /bin/sh (BusyBox ash)
```

La imagen de rootfs la produce el tooling de ISD. IR0 no inyecta binarios en
ella como parte del build del kernel.

## 4. Targets Make

| Target | Rol |
|--------|-----|
| `make run DISK=…` | Arrancar el ISO con un disco de usuarios construido por ISD |
| `make headers_install DESTDIR=…` | Exportar la UAPI pública (`includes/uapi/`) para un build externo |
| `make check-env PROFILE=userspace` | Toolchain del host para builds externos de ISD |
| `make ir0` | Solo ISO de kernel (sin usuarios) |

ISD es dueño de sus propios targets (`build`, `stage-rootfs`, `pack-minix`, …);
IR0 no los replica ni envuelve. Ver `Documentation/esp/USERSPACE.md` y
[`../USERSPACE.md`](../../USERSPACE.md).

Guía: [`USERSPACE.md`](../../USERSPACE.md), [`TOOLING.md`](../../TOOLING.md).

**Build number:** el `#N` es **local a la máquina**. Comparar hosts por SHA-256 y
procedencia, no solo por `#N`. El versionado de ISD es independiente del
contador del kernel.

Los aliases de `irinit` y los viejos targets de usuarios fueron eliminados.
Usar `make ir0` + `make run DISK=…` y los comandos propios de ISD.

## 5. Identidad

- Banner serial: `IR0 kernel <IR0_VERSION_STRING>` (`ir0_boot_serial_ready()`).
- `uname`: sysname `IR0`, nodename `unix`, version `IR0/Unix`.
