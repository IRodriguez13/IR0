# Acoplamiento IR0 (kernel) ↔ IR0-userspace

> **Última verificación:** 2026-07-25  
> **Fuente de verdad:** este archivo, `Makefile` (`IR0_USERSPACE_ROOT`, `check-userspace`, `headers_install`), hermano [IR0-userspace](https://github.com/IRodriguez13/IR0-userspace), y [TREE_CONTRACT](../../../IR0-desktop/Documentation/TREE_CONTRACT.md) si existe.  
> **Inglés:** [`../USERSPACE.md`](../USERSPACE.md)

## Límite

| Vive en **IR0** | Vive en **IR0-userspace** |
|-----------------|---------------------------|
| Kernel, drivers, UAPI (`includes/uapi/`) | runit, BusyBox, login/doas, `/etc` de producto |
| Fixtures de test (`setup/pid1/`) | Recetas de paquetes, servicios, perfiles rootfs |
| Targets Make que *delegan* | ELFs y `disk.img` bajo `out/` |

**Regla dura:** si PID 1 puede reemplazarlo sin recompilar el kernel, no vive en IR0.

El boot de producto siempre hace `kexecve("/sbin/init")`. No hay shell de depuración in-kernel ni harness `debug_bins/`.

## Layout sugerido

```text
parent/
├── IR0/                 # este árbol
├── IR0-userspace/       # https://github.com/IRodriguez13/IR0-userspace
└── IR0-desktop/         # producto desktop opcional + smokes DESK
```

```bash
export IR0_USERSPACE_ROOT=/ruta/a/IR0-userspace
export IR0_ROOT=/ruta/a/IR0          # desde el árbol userspace
```

## Cableado (lado kernel)

```bash
git clone https://github.com/IRodriguez13/IR0-userspace.git ../IR0-userspace
make headers_install DESTDIR=../IR0-userspace/out/sysroot
make check-userspace
make build-runit
make load-userspace-runit
make kernel-x64-userspace.iso
```

`make check-userspace` **falla** si falta el hermano (nunca skip silencioso como PASS).

## Cableado (lado userspace)

Desde `IR0-userspace`:

```bash
export IR0_ROOT=../IR0
make fetch build rootfs
```

Ver `IR0-userspace/README.md` y `userspace/README.md` en este árbol (solo puntero).

## Qué no reintroducir en este árbol

- Fuentes BusyBox / runit / doas o `/etc` de producto bajo `setup/`
- Shell mono in-kernel (`dbgshell`) o registro `debug_bins/`
- `#include` de userspace de producto en el link del kernel
