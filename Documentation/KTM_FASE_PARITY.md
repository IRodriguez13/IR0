# FASE → KTM parity map

> **Última verificación:** 2026-07-10  
> **Fuente de verdad:** `includes/ir0/ktm/*`, `ktm/`, `make ktm-run`, `make ktm-userdev-run`,  
> smokes `setup/pid1/init_fase*`, Makefile targets.  
> **Política:** el kernel ya no emite `[FASE` (arch-guard). Este documento coteja
> **intención de cada oleada FASE** con el análogo KTM — no renombra binarios históricos.

## Leyenda de estado

| Estado | Significado |
|--------|-------------|
| **COVERED** | Hay scenario/case KTM o checkpoint+assert con la misma intención y gate runnable |
| **PARTIAL** | Existe checkpoint/probe/evento tipado, pero falta scenario KTM o el smoke sigue siendo tag userspace legacy |
| **GAP** | Sin análogo KTM; deuda explícita |
| **HOST** | Validación de entorno (QEMU/ISO/Kconfig) — sigue en host; no es estado interno del kernel |

## Superficie KTM actual (referencia)

| Mecanismo | Qué cubre |
|-----------|-----------|
| Checkpoints | `BOOT_*`, `PROCESS_{CREATE,FORK,EXEC,EXIT,REAP}`, `MM_{MAP,UNMAP,FAULT}`, `SCHED_SWITCH`, `VFS_{MOUNT,UMOUNT}` |
| Scenarios (boot suite) | `process.lifecycle`, `ipc.pipe_lifecycle`, `mm.cow_fork`, `process.exec`, `process.fork_rollback` (`make ktm-run`) |
| Userdev | `/dev/ktm` + `libktm-user` + case `fork_wait_signal` (`make ktm-userdev-run`) |
| Probes | `mm.frames`, `proc.list` |
| Invariants | process list + frame bounds |
| Transport | líneas `KTM|…` + `KTM_SUITE_OK` / `KTM_USERDEV_OK` |

---

## Matriz por FASE

### MM / fork reclaim (39–47)

| FASE | Intención histórica | Análogo KTM | Estado | Notas / deuda |
|------|---------------------|-------------|--------|----------------|
| **39** | VMA / mmap / brk lazy | `KTM_CP_MM_MAP` / `UNMAP`; smoke userspace `FASE39_*` | PARTIAL | Checkpoint en mmap/munmap; falta scenario `mm.vma` + asserts de VMA |
| **40** | Fork COW + `FASE40_SUMMARY` | scenario `mm.cow_fork` + `KTM_CP_PROCESS_FORK` + `smoke-mm-cow-lazy` | COVERED | Real share-on-fork + WP break (`62cc512`/`496b55d`); KTM scenario = frame bound; A–F userspace en `smoke-mm-cow-lazy` |
| **41** | Exit reclaim / PMM orphan | `process.lifecycle` + `KTM_ASSERT_NO_FRAME_LEAK` | PARTIAL | Leak frames en scenario sintético; reclaim real post-exec sigue en smokes FASE41 |
| **42** | PT reclaim / frame balance | probes `mm.frames`; contadores `ir0_mm_*` / `paging_ir0_mm_*` | PARTIAL | Rename hecho; falta scenario `mm.page_tables` |
| **43** | Proc audit / OOM class | invariants `process.list`; `fase_audit` counters | PARTIAL | Sin serial; falta scenario OOM/recoverable |
| **44** | Ref/destroy / wait drain | `KTM_CP_PROCESS_REAP` + lifecycle | PARTIAL | Drain storms siguen como init_fase44_* |
| **45** | Fork rollback | scenario `process.fork_rollback` | COVERED | Alloc+free sin link; assert no process/frame leak |
| **46** | Fork no-recurse / heap / wait note | `fork_wait_signal` (parcial) | PARTIAL | Heap/no-recurse: GAP scenario |
| **47** | MM owner / steady-state class | probe frames + audit stub | PARTIAL | Scenario `mm.steady_state` |

### IPC / pipes (48–49)

| FASE | Intención | Análogo KTM | Estado | Deuda |
|------|-----------|-------------|--------|-------|
| **48** | pipe2 / FD lifetime | scenario `ipc.pipe_lifecycle` + snapshot | COVERED | Create/RW/close; FD table userspace sigue en smokes pipe2 |
| **49** | EOF/EPIPE / wake | `ipc.pipe_lifecycle` (EOF + `-EPIPE`) | COVERED | Wake/sleep path sigue en smokes legacy; no events `PIPE_*` tipados aún |

### Exec / shell / toolchain (50–52)

| FASE | Intención | Análogo KTM | Estado | Deuda |
|------|-----------|-------------|--------|-------|
| **50** | Exec/open ABI bring-up | scenario `process.exec` + `KTM_CP_PROCESS_EXEC` | COVERED | Checkpoint+invariants; argv/env ELF load sigue en ABI audits / smokes |
| **50B/C** | Pipe RW / open classify | `ipc.pipe_lifecycle` (RW) | PARTIAL | Open classify: GAP `vfs.open` |
| **51** | Shell / redir / wait wake | — | GAP | Case userdev `shell_redir` o smoke→libktm |
| **52** | TCC / large file / toolchain | — | GAP / HOST | Mayormente userspace; kernel solo reclaim — scenario opcional |

### Pseudo-FS / graphics / desktop path (53–58)

| FASE | Intención | Análogo KTM | Estado | Deuda |
|------|-----------|-------------|--------|-------|
| **53A/B** | fs/dev + posix pseudofs | — | GAP | Scenarios `vfs.devfs` / `vfs.pseudofs` o ABI audits existentes como evidencia HOST |
| **54A–C** | fbdev / input | — | GAP | T2 smokes; KTM case opcional |
| **55A–E** | Doom prereq / stub / doomgeneric | — | GAP / HOST | Fuera de KTM core; tags de producto |
| **57\*** | Reintegración / GUI paths | — | HOST | Docs `fase57-*`; no kernel `[FASE` |
| **58\*** | BusyBox ash / runit / coreutils | — | HOST | `smoke-fase58*`; migrar asserts a libktm-user a largo plazo |

---

## Resumen cuantitativo (honesto)

| Estado | Cantidad (filas de matriz arriba) |
|--------|-----------------------------------|
| COVERED | 40, 45, 48, 49, 50 (kernel gate mínimo) |
| PARTIAL | 39, 41–44, 46–47, 50B/C |
| GAP | 51, 52 (kernel), 53–55 |
| HOST | 57–58, parte 52/55 |

**Conclusión:** el **framework** KTM reemplaza el canal FASE en kernel. Los P0/P1 de paridad de scenarios (`mm.cow_fork`, `ipc.pipe_lifecycle`, `process.exec`, `process.fork_rollback`) están en la boot suite de `ktm-run`. Quedan PARTIAL/GAP en VMA profunda, page-tables, shell/TCC/fb y smokes userspace históricos.

## Gates actuales (no FASE)

```bash
rg '\[FASE' kernel mm fs includes/ir0 drivers ktm arch sched --glob '*.{c,h}'  # 0
make -s ktm-run          # suite pass=5
make -s ktm-userdev-run
make -s arch-guard
```

## Prioridad restante (P1→P2)

1. **P1** — `mm.page_tables` / `mm.steady_state` / `mm.vma` (39/42/47)  
2. **P1** — case userdev COW A–F si se quiere retirar `smoke-mm-cow-lazy`  
3. **P2** — shell/TCC/fb/input cases vía libktm-user (51–55)  
4. **P2** — events tipados `PIPE_*` (wake/sleep) si hace falta telemetría fina  
