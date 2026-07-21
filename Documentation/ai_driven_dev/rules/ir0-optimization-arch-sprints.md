<!-- IR0 AI dev rule: ir0-optimization-arch-sprints -->
<!-- alwaysApply: true -->
<!-- description: IR0 — sprints obligatorios de optimización, sanitización arquitectónica y buenas prácticas post-hito -->

# IR0 — Sprints de optimización y arquitectura (post-hito)

Tras cada **hito de tier u oleada** con gates CTR verdes, el trabajo **no** pasa directo al
siguiente feature grande. Primero se ejecuta al menos un **sprint acotado** de emprolijado,
sanitización arquitectónica y buenas prácticas. Tabla detallada y backlog en
`Documentation/ROADMAP.md` (sección *Sprints de optimización y arquitectura*).

## Cuándo aplicar

| Disparador | Acción mínima |
|------------|---------------|
| Oleada multi-subsistema cerrada (manifest/smoke verde) | ≥1 sprint ARCH + gates CTR |
| Nuevo syscall o ABI userspace estable | ARCH-1 o POSIX según área + host/ktest |
| Hot path tocado (sched, MM, IPC, syscalls) | ARCH-3 lifecycle + PERF-1 si hay evidencia de coste |
| Feature declarada “hecha” en roadmap | ARCH-4 log hygiene antes de marcar done |

**No** saltar sprints para “ganar velocidad” en el siguiente hito: la deuda en `syscalls.c`,
facades rotas y lifecycle incompleto cuesta más que un sprint de 1–2 días.

## Catálogo de sprints

| ID | Entrada (gate verde) | Foco | Entregable |
|----|----------------------|------|------------|
| **ARCH-1** | Manifest tier1/musl sin gaps | Split `kernel/syscalls.c` → submódulos (`io_syscalls.c` ✓, `time_syscalls.c`, `syscall_dispatch.c`) | Líneas monolith ↓; Makefile + glue intactos |
| **ARCH-2** | Cambio ≥2 subsistemas | `scripts/architecture_guard.py`; facades `includes/ir0/*`; sin `#include <drivers/...>` en portable | arch-guard OK; sin nuevas violaciones |
| **ARCH-3** | MM / process / IPC / fd / pipe | Acquire/release en todos los paths (`kernel-resource-lifecycle`) | Revisión explícita en informe oleada |
| **ARCH-4** | Feature estable en producción/smoke | `DEBUG_*` / Kconfig; quitar `serial_print` en hot paths; warnings compile | Build limpio o warnings documentados |
| **POSIX-1** | `smoke-musl-pthread` | `pthread_create` musl real; robust mutex teardown; `SA_RESTART` donde aplique | Smoke libc, no solo `clone` directo |
| **POSIX-2** | Cred multi-UID estable | `su`/`sudo` BusyBox en rootfs; sticky/setgid casos borde | Smoke multiuser en rootfs real |
| **PERF-1** | Hot path identificado | Medir primero; optimizar dentro del alcance; sin sacrificar correctitud | Nota en informe: qué y por qué |
| **PERF-2** | Build matrix / drivers Rust-C++ | Compilación incremental (`unibuild-rust`, ccache); evitar relink kernel completo por un `.o` driver | Target documentado en Makefile |

## Gates obligatorios (cierre de sprint)

```bash
make -s kernel-x64.bin
make -s arch-guard
make -s build-matrix-min
make -s -C tests/host run
# Si tocó ktest/QEMU: make kernel-tests y smokes del tier afectado
```

Oleada grande: añadir `make roadmap-phaseN-*` o smokes listados en `Documentation/ROADMAP.md`.

## Relación con otras reglas

| Regla | Rol en el sprint |
|-------|------------------|
| `ir0-userspace-monolith-debt.md` | ARCH-1: no expandir `syscalls.c` |
| `kernel-architecture-rigor.md` | ARCH-2: facades y Kconfig |
| `kernel-resource-lifecycle.md` | ARCH-3 |
| `kernel-error-handling.md` | errno consistente al refactor |
| `ir0-development-multiagent-format.md` | Informe oleada con tabla sprint cerrados |
| `ir0-roadmap-research-multiagent.md` | Un tier a la vez; no marcar done sin prueba |

## Anti-patrones

- Cerrar hito y abrir otro sin sprint cuando el diff tocó ≥2 subsistemas o creció el monolith.
- Refactor “de paso” sin gate ni smoke → hacer sprint ARCH dedicado o no refactorizar.
- Optimizar sin requisito ni hot path medido (ver `ir0-performance-requirement-first`).
- Declarar tier/sub-hito done sin smoke/ktest/host del sprint POSIX o ARCH correspondiente.

## Documentación

- Backlog priorizado y hitos futuros: `Documentation/ROADMAP.md` (+ espejo `Documentation/esp/ROADMAP.md`).
- Tras cerrar sprint: actualizar fila en roadmap si cambia % tier o deuda P0/P1.
