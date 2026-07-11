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
| Scenarios (boot suite) | `process.lifecycle`, `ipc.pipe_lifecycle`, `mm.cow_fork`, `mm.vma`, `mm.page_tables`, `mm.steady_state`, `vfs.devfs`, `shell.redir`, `mm.oom_class`, `process.wait_drain`, `graphics.fb`, `input.events0`, `vfs.open_flags`, `process.exec`, `process.fork_rollback` (`make ktm-run`, pass=15) |
| Userdev | `/dev/ktm` + `libktm-user` + `fork_wait_signal` (`ktm-userdev-run`) + `cow_touch` (`ktm-userdev-cow-run`) |
| Probes | `mm.frames`, `proc.list` |
| Invariants | process list + frame bounds |
| Transport | líneas `KTM|…` + `KTM_SUITE_OK` / `KTM_USERDEV_OK` |

---

## Matriz por FASE

### MM / fork reclaim (39–47)

| FASE | Intención histórica | Análogo KTM | Estado | Notas / deuda |
|------|---------------------|-------------|--------|----------------|
| **39** | VMA / mmap / brk lazy | scenario `mm.vma` + `KTM_CP_MM_MAP`/`UNMAP`; lazy still `CONFIG_LAZY_*` + smoke | COVERED | List insert/clone/teardown in `ktm-run`; deep lazy A–F remains userspace smoke |
| **40** | Fork COW + `FASE40_SUMMARY` | scenario `mm.cow_fork` + `KTM_CP_PROCESS_FORK` + `smoke-mm-cow-lazy` | COVERED | Real share-on-fork + WP break (`62cc512`/`496b55d`); KTM scenario = frame bound; A–F userspace en `smoke-mm-cow-lazy` |
| **41** | Exit reclaim / PMM orphan | `process.lifecycle` + `KTM_ASSERT_NO_FRAME_LEAK` | PARTIAL | Leak frames en scenario sintético; reclaim real post-exec sigue en smokes FASE41 |
| **42** | PT reclaim / frame balance | scenario `mm.page_tables` + `paging_ir0_mm_category_stats` | COVERED | Category alloc≥free in `ktm-run`; deep PT reclaim storms remain `init_fase42_*` smokes |
| **43** | Proc audit / OOM class | scenario `mm.oom_class` + `paging_fase43_oom_audit` hook | COVERED | Hook + frame bound en `ktm-run`; reclaim profundo / killer path sigue Future |
| **44** | Ref/destroy / wait drain | scenario `process.wait_drain` + `KTM_CP_PROCESS_REAP` | COVERED | N zombies sintéticos + reap; storm 512 sigue `init_fase44_*` HOST |
| **45** | Fork rollback | scenario `process.fork_rollback` | COVERED | Alloc+free sin link; assert no process/frame leak |
| **46** | Fork no-recurse / heap / wait note | `fork_wait_signal` + `cow_touch` userdev | COVERED (mínimo) | Heap/no-recurse profundo: HOST; A–F COW sigue `smoke-mm-cow-lazy` |
| **47** | MM owner / steady-state class | scenario `mm.steady_state` + `paging_fase47_steady_state_audit` | COVERED | Bounded frame growth in `ktm-run`; deep owner class still fase_audit smokes |

### IPC / pipes (48–49)

| FASE | Intención | Análogo KTM | Estado | Deuda |
|------|-----------|-------------|--------|-------|
| **48** | pipe2 / FD lifetime | scenario `ipc.pipe_lifecycle` + snapshot | COVERED | Create/RW/close; FD table userspace sigue en smokes pipe2 |
| **49** | EOF/EPIPE / wake | `ipc.pipe_lifecycle` (EOF + `-EPIPE`) | COVERED | Wake/sleep path sigue en smokes legacy; no events `PIPE_*` tipados aún |

### Exec / shell / toolchain (50–52)

| FASE | Intención | Análogo KTM | Estado | Deuda |
|------|-----------|-------------|--------|-------|
| **50** | Exec/open ABI bring-up | scenario `process.exec` + `KTM_CP_PROCESS_EXEC` | COVERED | Checkpoint+invariants; argv/env ELF load sigue en ABI audits / smokes |
| **50B/C** | Pipe RW / open classify | `ipc.pipe_lifecycle` + scenario `vfs.open_flags` | COVERED | Classify Linux→IR0 en boot suite; open path real sigue smokes |
| **51** | Shell / redir / wait wake | scenario `shell.redir` (pipe stand-in) | COVERED | Redir mínima en boot suite; ash/wait wake real sigue en smokes HOST |
| **52** | TCC / large file / toolchain | — | GAP / HOST | Mayormente userspace; kernel solo reclaim — scenario opcional |

### Pseudo-FS / graphics / desktop path (53–58)

| FASE | Intención | Análogo KTM | Estado | Deuda |
|------|-----------|-------------|--------|-------|
| **53A/B** | fs/dev + posix pseudofs | scenario `vfs.devfs` (`dev_null` open/close) | COVERED (53A) | 53B pseudofs profundo / ABI audits siguen HOST |
| **54A–C** | fbdev / input | `graphics.fb` + `input.events0` | COVERED (54A/B) | 54C deterministic / Doom siguen HOST |
| **55A–E** | Doom prereq / stub / doomgeneric | — | GAP / HOST | Fuera de KTM core; tags de producto |
| **57\*** | Reintegración / GUI paths | — | HOST | Docs `fase57-*`; no kernel `[FASE` |
| **58\*** | BusyBox ash / runit / coreutils | — | HOST | `smoke-fase58*`; migrar asserts a libktm-user a largo plazo |

---

## Resumen cuantitativo (honesto)

| Estado | Cantidad (filas de matriz arriba) |
|--------|-----------------------------------|
| COVERED | 39, 40, 42–46 (46 mínimo), 47–51, 50B, 53A, 54A/B |
| PARTIAL | 41, 53B |
| GAP | 52 (kernel), 55 |
| HOST | 57–58, parte 52/55, storms 41/42/44 |

**Conclusión:** Boot suite `ktm-run` = **pass=15**. Userdev: `fork_wait_signal` + `cow_touch`. Quedan HOST/GAP en TCC/Doom (52/55) y reclaim profundo (41).

## Gates actuales (no FASE)

```bash
rg '\[FASE' kernel mm fs includes/ir0 drivers ktm arch sched --glob '*.{c,h}'  # 0
make -s ktm-run              # suite pass=15
make -s ktm-userdev-run
make -s ktm-userdev-cow-run
make -s arch-guard
```

## Prioridad restante (P1→P2)

1. **P2** — TCC/Doom cases (52, 55) solo si se prioriza toolchain/GUI  
2. **P2** — events tipados `PIPE_*` (wake/sleep) si hace falta telemetría fina  
3. **P2** — reclaim post-exec profundo (41) más allá de leak asserts sintéticos  
4. **HOST** — A–F COW completo sigue en `smoke-mm-cow-lazy` (no retirar aún)
