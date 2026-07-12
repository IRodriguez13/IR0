# FASE → KTM parity map

> **Última verificación:** 2026-07-12  
> **Fuente de verdad:** `includes/ir0/ktm/*`, `ktm/`, `make ktm-run`, `make ktm-userdev-*`,  
> smokes `setup/pid1/init_fase*`, Makefile targets.  
> **Inventario de targets:** [`KTM_FASE_INVENTORY.md`](KTM_FASE_INVENTORY.md).  
> **Política:** el kernel ya no emite `[FASE` (arch-guard). Este documento coteja
> **intención de cada oleada FASE** con el análogo KTM — no renombra binarios históricos.

## Leyenda de estado

| Estado | Significado |
|--------|-------------|
| **COVERED** | Hay scenario/case KTM o checkpoint+assert con la misma intención y gate runnable |
| **PARTIAL** | Existe checkpoint/probe/evento tipado, pero falta scenario KTM o el smoke sigue siendo tag userspace legacy |
| **GAP** | Sin análogo KTM; deuda explícita |
| **HOST** | Validación de entorno (QEMU/ISO/Kconfig) — sigue en host; no es estado interno del kernel |
| **SUB** | Legacy `IR0_LEGACY_SMOKE` target **deprecated**; canonical gate is KTM (grace oleada: legacy still runs with WARN) |

## Superficie KTM actual (referencia)

| Mecanismo | Qué cubre |
|-----------|-----------|
| Checkpoints | `BOOT_*`, `PROCESS_{CREATE,FORK,EXEC,EXIT,REAP}`, `MM_{MAP,UNMAP,FAULT}`, `SCHED_SWITCH`, `VFS_{MOUNT,UMOUNT}` |
| Scenarios (boot suite) | 16 scenarios — wait_drain **N=64**, reclaim_exit **64** rounds, page_tables + churn 32 |
| Userdev | `fork_wait_signal`, `cow_touch`, **`fork_exit_storm`** (64+32 real fork/wait + KTM asserts) |
| Probes | `mm.frames`, `proc.list` |
| Product tags | `POWER_TCC_KTM_OK`, `KTM_DOOM_55D_OK`, `KTM_BUSYBOX_COREUTILS_OK` |
| Transport | líneas `KTM|…` + `KTM_SUITE_OK` / `KTM_USERDEV_*_OK` |

---

## Matriz por FASE

### MM / fork reclaim (39–47)

| FASE | Intención histórica | Análogo KTM | Estado | Notas / deuda |
|------|---------------------|-------------|--------|----------------|
| **39** | VMA / mmap / brk lazy | scenario `mm.vma` + `KTM_CP_MM_MAP`/`UNMAP`; lazy still `CONFIG_LAZY_*` + smoke | COVERED | List insert/clone/teardown in `ktm-run`; deep lazy A–F remains userspace smoke |
| **40** | Fork COW + `FASE40_SUMMARY` | scenario `mm.cow_fork` + `smoke-mm-cow-lazy` | COVERED | A–F HOST verificado; userdev `cow_touch` |
| **41** | Exit reclaim / PMM orphan | `process.reclaim_exit` (64) + `ktm-userdev-fork-storm-run` | COVERED+SUB | Legacy `smoke-userspace-fase41-reclaim` deprecated |
| **42** | PT reclaim / fork storm | `mm.page_tables` (+churn) + fork storm 64 | COVERED+SUB | Legacy `smoke-fork-exit-storm` deprecated |
| **43** | Proc audit / OOM class | scenario `mm.oom_class` | COVERED | Deep killer path Future |
| **44** | Ref/destroy / wait drain | `process.wait_drain` (64) + storm drain 32 | COVERED+SUB | Legacy 512 optional; exec/init-exit drain still HOST |
| **45** | Fork rollback | scenario `process.fork_rollback` | COVERED | |
| **46** | Fork no-recurse / heap / wait | `fork_wait_signal` userdev | COVERED (mínimo) | |
| **47** | MM owner / steady-state | scenario `mm.steady_state` | COVERED | |

### IPC / pipes (48–49)

| FASE | Intención | Análogo KTM | Estado | Deuda |
|------|-----------|-------------|--------|-------|
| **48** | pipe2 / FD lifetime | `ipc.pipe_lifecycle` | COVERED | |
| **49** | EOF/EPIPE / wake | `KTM_EVENT_PIPE_*` | COVERED | |

### Exec / shell / toolchain (50–52)

| FASE | Intención | Análogo KTM | Estado | Deuda |
|------|-----------|-------------|--------|-------|
| **50** | Exec/open ABI | `process.exec` + open_flags | COVERED | |
| **51** | Shell / redir | `shell.redir` | COVERED | ash real HOST |
| **52** | TCC / toolchain | `smoke-tcc-power-halt` + `POWER_TCC_KTM_OK` (snapshot frames>0) | HOST+KTM | Product |

### Pseudo-FS / graphics / desktop path (53–58)

| FASE | Intención | Análogo KTM | Estado | Deuda |
|------|-----------|-------------|--------|-------|
| **53A/B** | fs/dev + posix | `vfs.devfs` | COVERED (53A) | 53B HOST |
| **54A–C** | fbdev / input | `graphics.fb` + `input.events0` | COVERED (54A/B) | 54C HOST |
| **55A–E** | Doom | `smoke-fase55d` + `KTM_DOOM_55D_OK` | HOST+KTM | Product WAD |
| **57\*** | GUI paths | — | HOST | |
| **58\*** | BusyBox ash / coreutils | `KTM_BUSYBOX_COREUTILS_OK` on 58l | HOST+KTM | BUSY-1/2 ship |

---

## Resumen cuantitativo (honesto)

| Estado | Notas |
|--------|-------|
| COVERED / COVERED+SUB | 39–51 kernel intent; 41/42/44 fork depth SUB |
| HOST+KTM | 52 TCC, 55 Doom, 58 BusyBox instrumented |
| HOST residual | 44 exec/init-exit, 53B, 54C, 57 |

**Gates:**

```bash
make -s ktm-run
make -s ktm-userdev-run ktm-userdev-cow-run ktm-userdev-fork-storm-run
make -s arch-guard
```

## Prioridad restante

1. BUSY-1/2 + VM manual (ship 0.0.1)  
2. Deprecate remaining HOST-only FASE44 exec/init after KTM cases exist  
3. F8 TCP / ARM64 full link
