# Documentacion IR0 en Espanol

> **Última verificación:** 2026-11-06
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
- `EARLY_FOLD.md` — adaptador `early_*` vs VFS/`ramtab`/`ipc/` y providers ARM
- `VIRTUAL_FILESYSTEMS.md`
- `DRIVERS.md`
- `INTERRUPTS.md`
- `MEMORY.md`
- `LINUX_SHAPED.md` — nombres/ABI Linux sin clonar el árbol
- `HARNESS_MAP.md` — tags y targets semánticos; smokes ARM64 opcionales (`smoke-arm64-*`)
- `../includes/ir0/smp_convergence.h` — mapa diseño Linux→IR0 para SMP/red (solo comentarios)
- `testing/KTEST_FIX_CONTRACTS.md` — 13 casos del ktest de boot (canónico: `../testing/KTEST_FIX_CONTRACTS.md`)
- `uaccess.md` — frontera `copy_*_user` / `copy_*_user_mm` multi-ISA
- `PROCESSES.md`
- `SCHEDULING.md`
- `UNIX_DIFFERENCES.md`
- `KLOG.md` / `KTM.md` / `STABLE.md` / `BACKLOG_REMAINING.md` (espejos donde existan)

Capítulos internos del kernel (iniciativa mandocs, espejo bilingüe):

- `../mandocs/esp/INDEX.md` — índice y plan por oleadas
- `../mandocs/esp/vfs.md` — VFS y enrutamiento (`man IR0-vfs-es` tras `make mandocs-es`)

Reglas para agentes de IA (solo ingles): `Documentation/ai_driven_dev/`.
