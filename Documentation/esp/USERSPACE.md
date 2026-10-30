# IR0 kernel ↔ usuarios ISD

> **Última verificación:** 2026-10-29
> **Fuente de verdad:** este archivo, `Makefile` raíz, `IR0_ISD_INTERFACE`,
> hermano [ISD](https://github.com/IRodriguez13/ISD), [SETUP.md](../../SETUP.md).
> **English:** [`../USERSPACE.md`](../USERSPACE.md)

## Frontera

El kernel no es una distro. PID1 (**runit**), **BusyBox**, login, paquetes,
rootfs y `disk.img` viven en **ISD**. IR0 compila el kernel y arranca una
imagen de usuarios que otro árbol construyó.

| Vive en **IR0** | Vive en **ISD** |
|-----------------|-----------------|
| Kernel, drivers, UAPI (`includes/uapi/`) | Paquetes, servicios, rootfs, perfiles |
| ISO de kernel / imágenes ARM64 | `out/<arch>/images/<profile>/disk.img` |
| Launcher QEMU (`scripts/run_qemu.sh`) | Extras `.isdconfig`, stage/pack |

**Regla dura:** el Makefile de IR0 no clona, compila ni empaqueta ISD. No hay
targets `first-boot`, `isd`, `run PROFILE=…`, `kmang` ni `poweron`. El grafo del
kernel se completa sin un checkout hermano.

## Construir y arrancar

### 1. Kernel

```bash
git clone https://github.com/IRodriguez13/IR0.git
cd IR0
make check-env
make defconfig
make ir0
```

Equivalente ARM64:

```bash
make ir0 arm64
make ir0 arm64 rpi4
```

### 2. Imagen de usuarios

Construí el hermano ISD con su propio Makefile/herramientas:

```bash
git clone https://github.com/IRodriguez13/ISD.git ../ISD
make -C ../ISD help
```

Los nombres de targets de ISD pertenecen a ese repositorio; IR0 no los envuelve.

### 3. Arrancar

```bash
make run DISK=../ISD/out/x86_64/images/minimal/disk.img
```

`DISK` es opcional. Sin disco, el kernel arranca el ISO y entra en pánico en el
handoff a `/sbin/init` — no hay shell de respaldo en el kernel. `DISK=none` evita
adjuntar un `disk.img` local.

`make run` nunca compila. Reconstruí el kernel con `make ir0` tras cambiar
fuentes, y la imagen de usuarios con ISD tras cambiar usuarios.

## Contrato de interfaz

| Archivo | Repo | Rol |
|---------|------|-----|
| `IR0/IR0_ISD_INTERFACE` | IR0 | Versión de integración (`VERSION=1`) + adapters requeridos |
| `ISD/IR0_ISD_INTERFACE_SUPPORTED` | ISD | Versión máxima soportada por ese checkout |

IR0 exporta UAPI con `make headers_install DESTDIR=…`; un build externo de
usuarios consume esa exportación. ISD valida la versión antes de empaquetar.

## Capas de configuración

| Archivo | Dueño | Significado |
|---------|-------|-------------|
| `IR0/.config` | kernel | Kconfig |
| `ISD/profiles/<p>/profile.conf` | ISD | login/root/fsck |
| `ISD/profiles/<p>/packages.txt` | ISD | paquetes obligatorios |
| `ISD/.isdconfig` | ISD | paquetes/applets opcionales |

`PROFILE=…` es un concepto de ISD. Ya no es una variable del build del kernel;
el kernel elige `ARCH` y, en ARM64, `BOARD`.

## ARM64

ARM64 QEMU es un target de **kernel** (`qemu-virt`, imágenes RPi3/4/5). ISD no
tiene hoy rootfs producto AArch64. No asumas que un `disk.img` x86-64 se puede
adjuntar a un boot ARM64.

## Contratos del lado kernel

### Scripts con shebang (`#!`)

`execve` / `exec_replace_current` en `kernel/elf_loader.c` reconocen una línea
`#!` antes de validar ELF: ruta del intérprete, argumento opcional y luego la
ruta del script agregada a `argv`. La recursión está limitada a 4; archivos no
ELF sin shebang válido devuelven `-ENOEXEC`.

### Login / getty

Tras el firstboot de ISD, `etc/runit/sv/console/run` (`runit_console_run`) pide
usuario/password. Los marcadores de smoke (`LOGIN_USER_READ`) van solo a
**`/dev/serial`**. Emitir el tag en cada lectura vacía/`EINTR`/`EOF` generaba un
busy-loop; corregido en ISD (`ir0_read_line` reintenta `EINTR`, `r==0` es EOF,
tag solo con username no vacío).

## Versionado de usuarios (ISD)

| Capa | Mecanismo |
|------|-----------|
| Release ISD | `ISD/VERSION` → `VERSION_ID` + `/usr/share/isd/release.txt` |
| Orígenes de paquetes | `/usr/share/isd/package-manifest.txt` |
| Base userland | `USERLAND_BASE=busybox` (`coreutils` reservado) |
| Guest | `ir0-status version \| packages \| userland` |
| Host | `python3 scripts/userspace_manager.py` (inspector ISD; no es target make) |

IR0 se adapta al ABI Linux/musl; no se parchean clientes upstream. Ver
[`../DESKTOP_ABI.md`](../DESKTOP_ABI.md).
