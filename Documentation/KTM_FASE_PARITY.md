# FASE → KTM parity map

> **Última verificación:** 2026-07-11  
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
| Scenarios (boot suite) | … + `process.reclaim_exit` (`make ktm-run`, **pass=16**) |
| Userdev | `fork_wait_signal` + `cow_touch` |
| Probes | `mm.frames`, `proc.list` |
| Invariants | process list + frame bounds |
| Transport | líneas `KTM|…` + `KTM_SUITE_OK` / `KTM_USERDEV_OK` |

---

## Matriz por FASE

### MM / fork reclaim (39–47)

| FASE | Intención histórica | Análogo KTM | Estado | Notas / deuda |
|------|---------------------|-------------|--------|----------------|
| **39** | VMA / mmap / brk lazy | scenario `mm.vma` + `KTM_CP_MM_MAP`/`UNMAP`; lazy still `CONFIG_LAZY_*` + smoke | COVERED | List insert/clone/teardown in `ktm-run`; deep lazy A–F remains userspace smoke |
| **40** | Fork COW + `FASE40_SUMMARY` | scenario `mm.cow_fork` + `smoke-mm-cow-lazy` | COVERED | A–F HOST verificado 2026-07-11 (`smoke-mm-cow-lazy` PASS) |
| **41** | Exit reclaim / PMM orphan | scenario `process.reclaim_exit` + `smoke-userspace-fase41-reclaim` | COVERED | Rounds sintéticos en boot; storm mmap/exec sigue HOST |
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
| **49** | EOF/EPIPE / wake | scenario `ipc.pipe_lifecycle` + `KTM_EVENT_PIPE_*` | COVERED | CREATE/EOF/EPIPE/WAKE tipados en `pipe.c` |

### Exec / shell / toolchain (50–52)

| FASE | Intención | Análogo KTM | Estado | Deuda |
|------|-----------|-------------|--------|-------|
| **50** | Exec/open ABI bring-up | scenario `process.exec` + `KTM_CP_PROCESS_EXEC` | COVERED | Checkpoint+invariants; argv/env ELF load sigue en ABI audits / smokes |
| **50B/C** | Pipe RW / open classify | `ipc.pipe_lifecycle` + scenario `vfs.open_flags` | COVERED | Classify Linux→IR0 en boot suite; open path real sigue smokes |
| **51** | Shell / redir / wait wake | scenario `shell.redir` (pipe stand-in) | COVERED | Redir mínima en boot suite; ash/wait wake real sigue en smokes HOST |
| **52** | TCC / large file / toolchain | legacy `smoke-fase52-tcc` | HOST | No scenario KTM en kernel; producto userspace |

### Pseudo-FS / graphics / desktop path (53–58)

| FASE | Intención | Análogo KTM | Estado | Deuda |
|------|-----------|-------------|--------|-------|
| **53A/B** | fs/dev + posix pseudofs | scenario `vfs.devfs` (`dev_null` open/close) | COVERED (53A) | 53B pseudofs profundo / ABI audits siguen HOST |
| **54A–C** | fbdev / input | `graphics.fb` + `input.events0` | COVERED (54A/B) | 54C deterministic sigue HOST |
| **55A–E** | Doom prereq / stub / doomgeneric | smokes T2 / STABLE | HOST | Fuera de KTM core; no GAP de kernel |
| **57\*** | Reintegración / GUI paths | — | HOST | Docs `fase57-*`; no kernel `[FASE` |
| **58\*** | BusyBox ash / runit / coreutils | — | HOST | `smoke-fase58*`; migrar asserts a libktm-user a largo plazo |

---

## Resumen cuantitativo (honesto)

| Estado | Cantidad (filas de matriz arriba) |
|--------|-----------------------------------|
| COVERED | 39–51 (mínimos), 50B, 53A, 54A/B |
| PARTIAL | 53B |
| HOST | 52, 55, 57–58, storms profundos 42/44 |

**Conclusión:** Boot suite `ktm-run` = **pass=16**. Open KTM residual cerrado. Future F1–F6 cerrados; F7.1 ARM64 early MMU cerrado (`smoke-arm64-mmu`); siguiente = **F7.2 VBAR/SVC/EL0**.

## Gates actuales (no FASE)

```bash
make -s ktm-run              # suite pass=16
make -s ktm-userdev-run ktm-userdev-cow-run
make -s smoke-mm-cow-lazy
make -s smoke-nvme-read
make -s arch-guard
```

## Prioridad restante

1. **Future F7.2** — ARM64 VBAR/SVC / userspace (ver `BACKLOG_REMAINING.md`)  
2. HOST opcional — TCC/Doom stable (STABLE.md)  
3. **P2** — kexec_load / S3 resume reales
