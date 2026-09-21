# IR0 Userspace Bootstrap

| Campo | Valor |
|-------|-------|
| Version | 0.3 |
| Fase IR0 | T1–T2 |
| Estado | estable |
| Depende de | boot, process, vfs, tty |
| Man page | IR0-userspace (sección 7) |
| Fuentes principales | `kernel/main.c`, `scripts/make/isd.mk`, `scripts/kernel_manager.py`, `scripts/userspace_manager.py`, repo hermano `ISD/` |

> **Última verificación:** 2026-09-18

> **Nota (2026-09-18):** el rootfs de producto y los perfiles se construyen desde el
> repo hermano **`ISD/`** (`IR0_ISD_ROOT`). El kernel dispara boot vía `scripts/make/isd.mk`:
> discos persistentes, `make kmang` (TUI catálogo ISO) y `make usmang` (inspector ISD
> en host). El alias legacy `IR0-userspace/` apunta a `IR0_ISD_ROOT`.

> **Nota (2026-07-24):** el PID1 transitorio **irinit** se eliminó. Producto y tests usan solo **runit** (`make build-runit` / `load-userspace-runit` / `smoke-runit-boot`).

## 1. Resumen

El boot de producción siempre carga **`/sbin/init`** vía `kexecve` desde `kmain`.
El PID1 canónico es **runit** (`IR0-userspace/out/bin/runit-init`
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
| `make kmang` | Catálogo interactivo de ISO kernel; `poweron` arranca Default — ver **`man IR0-kmang`** |
| `make usmang` | Resumen ISD en host; detalle desktop solo con `PROFILE=desktop` |
| `make smoke-x11-pointer PROFILE=desktop` | TinyX fullscreen + clientes desktop |

Guía: [`USERSPACE.md`](../../USERSPACE.md), [`TOOLING.md`](../../TOOLING.md).

**kmang:** el `#N` de build es **local a la máquina**. Comparar hosts por SHA-256 y
procedencia, no solo por `#N`. La TUI avisa si el ISO Workspace difiere del Default.
Contrato completo: **`man IR0-kmang`**.

**usmang:** lee `ISD/VERSION` y `profiles/<PROFILE>/packages.txt` reales; clientes X
solo con `PROFILE=desktop`. Versión ISD ≠ build local del kernel.

Aliases retirados (fail-fast): `build-irinit`, `load-userspace-irinit`,
`smoke-userspace-irinit`, `run-irinit-interactive-gui`.

## 4. Identidad

- Banner serial: `IR0 kernel <IR0_VERSION_STRING>` (`ir0_boot_serial_ready()`).
- `uname`: sysname `IR0`, nodename `unix`, version `IR0/Unix`.
