# Mapa de harness (tags y nombres Make)

> **Nota de build (2026-10-29):** este documento es un inventario histórico de
> nombres de smoke/KTM. La mayoría de los targets `make smoke-*` ya no existen.
> Gates actuales: `make ctr`, `make agent-fast`, `make ktest`,
> `make build-matrix-min`, `make arch-guard`.

> **Última verificación:** 2026-10-28
> **Fuente de verdad:** `Documentation/CHANGELOG.md`, `scripts/ktm_*.py`,
> `setup/pid1/ktm_*.c`, `setup/doom/doomgeneric_ir0.c`

Nombres canónicos de smokes QEMU, casos KTM y tags serial.
Los identificadores `FASE*` de oleada sobreviven solo como **contratos
externos**; el kernel ya no los imprime.

Canónico en inglés: [`../HARNESS_MAP.md`](../HARNESS_MAP.md).

## Tags serial (invitado)

| Área | Tag ahora | Token viejo (retirado en C) |
|------|-----------|-----------------------------|
| Heap / brk | `HEAP_SMOKE` | `FASE39*` |
| mmap | `MMAP_SMOKE` | `FASE39*` |
| Fork COW | `MM_COW_*` | `FASE40*` |
| Reclaim / PT | `RECLAIM_*` | `FASE41` / `FASE42` |
| IPC | `IPC_*` | `FASE48` |
| Pipe | `PIPE_SMOKE` | `FASE49` |
| PID1 `_exit` | `INIT_EXIT_DRAIN` | `FASE44` |
| Doom producto | `KTM_DOOMGENERIC_OK` / `KTM_DOOMGENERIC_CASE_OK` | `FASE55D_DOOMGENERIC_OK` / `KTM_DOOM_55D_OK` |

Debug residual: `IR0_DEBUG_PROC` (ya no `CONFIG_DEBUG_FASE50`).

## Targets Make

El catálogo de alias `smoke-fase*` / `build-*-fase*` se eliminó el 2026-10-28
junto con `setup/make/legacy-smokes.mk`. Solo existen los nombres canónicos; ya
no hay compuerta `IR0_LEGACY_SMOKE`.

| Área | Target |
|------|--------|
| Gate rápido del kernel | `make test-fast` |
| Suite dentro del kernel | `make ktest` |
| Gate arquitectónico completo | `make ctr` / `make agent-fast` |
| Inventario KTM | `make ktm-check` / `make ktm-manifest` |
| Producto x86-64 | `make ir0` / `make run [gui\|console\|debug]` |
| Agregado ARM64 | `make ir0 arm64` / `make run arm64 console` |
| Imagen Raspberry Pi | `make ir0 arm64 rpi3` (o `rpi4`, `rpi5`) |

Los paquetes de userspace (BusyBox, runit, TinyCC e imágenes ISD) se construyen
en el repositorio ISD; el Makefile del kernel ya no actúa como proxy.

Tabla completa en el documento inglés.

## Conservar (no renombrar)

- Env `FASE50_BUSYBOX_BIN` (ISD)
- `packages/busybox/fase58_*.config` (ISD)
- `setup/pid1/fase52_staging/`
- `runit_fase55d_*` en stage-bin de ISD
