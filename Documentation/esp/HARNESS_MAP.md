# Mapa de harness (tags y nombres Make)

> **Última verificación:** 2026-10-16
> **Fuente de verdad:** `setup/make/legacy-smokes.mk`, `scripts/make/testing.mk`,
> `setup/pid1/ktm_*.c`, `setup/doom/doomgeneric_ir0.c`

Nombres canónicos de smokes QEMU, casos KTM y tags serial.
Los identificadores `FASE*` de oleada son **alias o contratos externos**.

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

Preferir `smoke-ipc`, `smoke-pipe`, `smoke-busybox`, `smoke-tcc`,
`smoke-doomgeneric`, `smoke-ash-interactive`, `smoke-arm64-all`,
`build-busybox-min`, `build-tcc`. Los `smoke-fase*` / `build-*-fase*` siguen
como alias. Tags ARM: `ARM64_PRODUCT_VFS_OK`, `ARM64_EARLY_PATH_FOLD_OK`.

Tabla completa en el documento inglés.

## Conservar (no renombrar)

- Env `FASE50_BUSYBOX_BIN` (ISD)
- `packages/busybox/fase58_*.config` (ISD)
- `setup/pid1/fase52_staging/`
- `runit_fase55d_*` en stage-bin de ISD
