# Documentacion IR0 en Espanol

> **Última verificación:** 2026-09-22
> **Fuente de verdad:** espejos listados abajo; canónico en `Documentation/`.

Este directorio contiene traducciones de la documentacion tecnica principal.

## Politica

- La fuente primaria se mantiene en ingles en `Documentation/`.
- Esta carpeta mantiene el espejo en espanol por archivo.
- Mantener paridad funcional y evitar texto aspiracional.

## Mapa de Archivos

- `README.md`
- `USERSPACE.md` — acoplamiento kernel ↔ [ISD](https://github.com/IRodriguez13/ISD)
- `virtio.md` — virtio-9p / virtio-net y chaos de sesión
- `DECOUPLING.md`
- `MAKEFILE.md`
- `TOOLING.md`
- `FILESYSTEM.md`
- `VIRTUAL_FILESYSTEMS.md`
- `DRIVERS.md`
- `INTERRUPTS.md`
- `MEMORY.md`
- `LINUX_SHAPED.md` — nombres/ABI Linux sin clonar el árbol
- `HARNESS_MAP.md` — tags y targets semánticos (`smoke-fase*` = alias)
- `uaccess.md` — frontera `copy_*_user` / `copy_*_user_mm` multi-ISA
- `PROCESSES.md`
- `SCHEDULING.md`
- `UNIX_DIFFERENCES.md`
- `KLOG.md` / `KTM.md` / `STABLE.md` / `BACKLOG_REMAINING.md` (espejos donde existan)

Capítulos internos del kernel (iniciativa mandocs, espejo bilingüe):

- `../mandocs/esp/INDEX.md` — índice y plan por oleadas
- `../mandocs/esp/vfs.md` — VFS y enrutamiento (`man IR0-vfs-es` tras `make mandocs-es`)

Reglas para agentes de IA (solo ingles): `Documentation/ai_driven_dev/`.
