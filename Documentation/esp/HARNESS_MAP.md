# Mapa de harness (tags y nombres Make)

> **Última verificación:** 2026-10-28
> **Fuente de verdad:** `scripts/make/testing.mk`,
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
| Tier-1 arranque + ash | `smoke-tier1`, `run-ash-gui`, `check-ash-logs` |
| Arranque producto runit | `smoke-runit-boot`, `smoke-runit-login`, `smoke-runit-power` |
| BusyBox | `build-busybox-min`, `build-busybox-plus`, `build-busybox-full` |
| TinyCC | `build-tcc` |
| Smokes KTM en invitado | helpers `build-ktm-*` y sus runners `smoke-*` |
| Plano de datos COW | `smoke-mm-cow-lazy` |
| ARM64 | `smoke-arm64-all` y los targets `smoke-arm64-*` |

`build-init-*` compila el init ring-3 que inyecta `make load-init`;
`make build-init-musl` genera la variante musl estática de `init_musl.c`.

Tabla completa en el documento inglés.

## Conservar (no renombrar)

- Env `FASE50_BUSYBOX_BIN` (ISD)
- `packages/busybox/fase58_*.config` (ISD)
- `setup/pid1/fase52_staging/`
- `runit_fase55d_*` en stage-bin de ISD
