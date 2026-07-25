# IR0 Userspace Bootstrap

| Campo | Valor |
|-------|-------|
| Version | 0.2 |
| Fase IR0 | T1–T2 |
| Estado | estable |
| Depende de | boot, process, vfs, tty |
| Man page | IR0-userspace (sección 7) |
| Fuentes principales | `kernel/main.c`, `kernel/rootfs_base.c`, `scripts/inject_init_minix.py`, `Makefile`, repo hermano `IR0-userspace/` |

> **Última verificación:** 2026-07-25

> **Nota (2026-07-25):** el userspace Unix (runit, BusyBox, login, doas, `/etc`) vive en el repositorio hermano **`IR0-userspace`**. El kernel conserva solo sus fixtures de test (`setup/pid1/`) más el acoplamiento (`userspace/README.md`, `Documentation/USERSPACE.md`) y dispara la build de producto vía `IR0_USERSPACE_ROOT`; si falta el hermano, `check-userspace` falla en vez de saltear el gate. Se retiraron `debug_bins/` y el dbgshell in-kernel.

> **Nota (2026-07-24):** el PID1 transitorio **irinit** se eliminó. Producto y tests usan solo **runit** (`make build-runit` / `load-userspace-runit` / `smoke-runit-boot`).

## 1. Resumen

El boot de producción carga **`/sbin/init`** vía `kexecve` desde `kmain` cuando
`KERNEL_DEBUG_SHELL=0`. El PID1 canónico es **runit** (`IR0-userspace/out/bin/runit-init`
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
| `busybox-1.36.1` | Applets; receta y configs en `IR0-userspace/packages/busybox/` |

**Comportamiento runit:** stage 1 prepara el rootfs; stage 2 supervisa
servicios; la consola hace getty/login (`runit_console_run`): auth contra
`/etc/passwd`+`/etc/shadow` (`crypt(3)` musl), `setgid`/`setgroups`/`setuid`/
`chdir`, export de `HOME`/`USER`/`HOSTNAME`, y exec de ash como **login shell**
(`argv[0] = "-sh"`) para que corra `/etc/profile`. BusyBox de producto:
`CONFIG_ASH_EXPAND_PRMT` + `CONFIG_ASH_TEST`. Prompt: `# ` (root) o
`user@host:$PWD$ ` (resto). Cuentas: `root` (vacía), `ir0`/`ivan` (MD5 crypt).
Smokes: `smoke-runit-login`, `smoke-runit-login-nonroot`.

**Perfiles de producto.** `/etc/ir0-profile` (lo escribe
`IR0-userspace/scripts/install-to-disk.sh` a partir de `IR0_PRODUCT_PROFILE`)
define la política de consola: `development` mantiene autologin root con
advertencia visible, `desktop` muestra `hostname login:` y bloquea el login
directo de root con `/etc/ir0-noroot` (nombre ≤14 bytes por el límite de
entradas de directorio de MINIX v1), `appliance` no abre login interactivo
(`CONSOLE_NO_LOGIN`).

## 3. Targets Make

| Target | Rol |
|--------|-----|
| `make headers_install DESTDIR=…` | Exportar la UAPI pública (`includes/uapi/`) al userspace |
| `make build-runit` | Delega en `IR0-userspace` (runit + ELFs de servicio) |
| `make load-userspace-runit` | Formatear disco MINIX e instalar el rootfs del hermano |
| `make smoke-runit-boot` | Smoke headless de PID1 |
| `make smoke-runit-login` | Autologin root (password vacío) |
| `make smoke-runit-login-nonroot` | No-root: crypt(3) + uid 1001 + PS1 |
| `make run-fase58e-ash-gui` | ash interactivo GTK |

Aliases retirados (fail-fast): `build-irinit`, `load-userspace-irinit`,
`smoke-userspace-irinit`, `run-irinit-interactive-gui`.

## 4. Identidad

- Banner serial: `IR0 kernel <IR0_VERSION_STRING>` (`ir0_boot_serial_ready()`).
- `uname`: sysname `IR0`, nodename `unix`, version `IR0/Unix`.
