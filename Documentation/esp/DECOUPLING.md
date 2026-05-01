# Mapa de desacople del kernel IR0

Este documento es la versión en español de `Documentation/DECOUPLING.md`. La documentación
técnica principal se mantiene en inglés en el directorio `Documentation/`; aquí se resume el
mismo contenido sin mezclar idiomas.

## Objetivos de diseño

- **Facades:** Las capas superiores (`fs/` y rutas de syscalls) deben usar `includes/ir0/*`
  antes que incluir directamente `drivers/*` o `arch/*`.
- **Callbacks / tablas de operaciones:** Los drivers exponen comportamiento mediante punteros
  a función (`ir0_driver_ops_t`, `block_dev_ops_t`, `vfs_ops`, `devfs_ops_t`, bootstrap).
- **Configuración:** Subsistemas opcionales mediante `CONFIG_*` en `setup/Kconfig`, `.config`,
  `Makefile` y `scripts/kconfig/menuconfig.py`.

Gran parte de `kernel/`, `mm/` y `net/` enruta ya **serial** y **reloj** vía `ir0/serial_io.h` y
`ir0/clock.h`. `fs/` usa `ir0/arch_port.h` para consultas de CPU (sin `#include <arch/...>` en el
fuente). Persisten otras facadas finas y `main.c` con política de arranque que aún referencia
drivers directos donde hace falta.

## Inventario de fachadas (`includes/ir0/`)

Ver tabla detallada en `Documentation/DECOUPLING.md`. Áreas cubiertas: disco (`block_dev`,
`partition`), consola (`console_backend`), tiempo (`clock`, `rtc`), red (`net`), entrada,
vídeo, serial para depuración, modelo de drivers (`driver`, `driver_bootstrap`),
pseudo-VFS (`devfs`, `procfs`, `sysfs`).

## Patrones de registro

- **Registro de drivers:** `ir0_register_driver` + `ir0_driver_ops_t`.
- **Bloque:** `block_dev_register` + `block_dev_ops_t`.
- **Sistemas de archivos:** `vfs_ops` / `vfs_fstype`.
- **Carácter (/dev):** `devfs_ops_t`.
- **Arranque por fases:** `driver_bootstrap_register` + `driver_boot_init_fn`.

## Estado multi-arquitectura

- **x86-64:** `kernel-x64.bin` enlaza **`$(ALL_OBJS)`**: kernel completo (Multiboot, etc.).
- **ARM64:** `kernel-arm64.bin` enlaza **solo `$(ARCH_OBJS)`**: andamiaje (`boot_stub`, stubs);
  no equivale al kernel completo enlazado en x86-64.

Objetivos futuros (USB, ARM estable, arranque en x86 y ARM) requieren ampliar el cierre de
objetos en ARM64, integración de syscalls/mm/fs y nuevos backends detrás de facelas y Kconfig.

## Fugas conocidas de acoplamiento

Listado cualitativo actualizado en `Documentation/DECOUPLING.md`: `kernel/main.c` (política de
drivers opcionales), backends de vídeo/entrada, y algunas facadas IR0 que siguen delegando en
drivers. La comprobación `scripts/architecture_guard.py` rechaza `#include <arch/…>` dentro de `fs/` y
`#include <interrupt/arch/…>` en `fs/`, `kernel/`, `mm/`, `net/`.

Los drivers que incluyen `kernel/resource_registry.h` o cabeceras `arch/` para IRQ/puertos
es un acoplamiento esperado para hardware tipo PC.

## Composición configuración ↔ build

La cadena defconfig/menuconfig → `Makefile` → flags y listas de objetos está descrita en
inglés en `DECOUPLING.md` y en `TOOLING.md`.

- **Digest estable de símbolo export:** `python3 scripts/kernel_export_digest.py kernel-x64.bin` (véase `DECOUPLING.md`).

## Rutas relacionadas en el código

`includes/ir0/driver.h`, `drivers/driver_bootstrap.c`, `drivers/storage/block_dev.h`,
`fs/vfs.h`, `includes/ir0/devfs.h`, `setup/Kconfig`, `Makefile`.

---

*Para texto completo tabular y líneas relacionadas CTR, véase `Documentation/DECOUPLING.md`.*
