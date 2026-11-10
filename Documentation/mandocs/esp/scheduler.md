# Planificador de IR0

> **Última verificación:** 2026-11-09
> **Fuente de verdad:** `includes/ir0/sched.h`, `sched/core.h`, `sched/core.c`, `sched/rr.c`, `sched/prio.c`

| Campo | Valor |
|-------|-------|
| Versión | 0.3 |
| Fase IR0 | T0 |
| Estado | stable |
| Depende de | process, memory |
| Página man | IR0-scheduler (sección 7) |
| Fuentes principales | `includes/ir0/sched.h`, `sched/core.h`, `sched/core.c`, `sched/rr.c`, `sched/prio.c`, `arch/common/arch_portable.h`, `sched/switch/switch_x64.asm`, `drivers/timer/clock_system.c` |

## 1. Visión general

IR0 planifica procesos mediante una **fachada portable**, un **núcleo** y
**un archivo de política** enlazado según `CONFIG_SCHEDULER_POLICY`. El defconfig usa **bandas de
prioridad** (`=2`). Round-robin (`0`) y el alias de nombre CFS (`1`) siguen
disponibles. Planificación **un CPU**; preemption por timer diferida.

La primera transferencia es `switch_to(NULL, next)`, que delega en el hook
de arquitectura `first_switch_to()`.
El código portable **no** embebe `iretq`.

## 2. Arquitectura interna

| Pieza | Rol |
|-------|-----|
| `includes/ir0/sched.h` | API portable (sin `<sched/…>`) |
| `sched/core.c` | `schedule()`, `sched_switch_to()`, `sched_take_resched()` |
| `sched/core.h` | Contrato privado núcleo/política (`sched_pick_next`, `rq_lock`/`rq_unlock`) |
| `sched/prio.c` | Política por defecto (policy 2) |
| `sched/rr.c` | Lista de listos (policy 0 / 1) |
| `sched/switch/context_switch.c` | `switch_to(prev, next)` → `__switch_to()` de la arquitectura |
| `sched/switch/kernel_resume.c` | Estado de syscall bloqueada + recuperación Class B |
| `sched/switch/diag.c` | `switch_trace()`: tags CTX CLASSIFY |

**Policy `1` (`cfs`):** mismo objeto de política que RR; no existe `cfs_sched.c`
ni un wrapper que incluya el runqueue de RR.

## 3. Flujo

`schedule()` → `sched_pick_next()` de la política → `sched_switch_to` →
`switch_to(NULL, next)` (primera vez) / `switch_to(prev, next)`, que vuelve
cuando `prev` corre otra vez sobre su propia pila de kernel.

## 4. Límites del subsistema

- Callers → solo `<ir0/sched.h>`.
- Una política no incluye ni llama a otra; ambas implementan `sched/core.h`.
- Sin `iretq` inline en código sched portable.

## 5. Relaciones

| Vecino | Interacción |
|--------|-------------|
| Process | `sched_add` / `sched_remove` |
| Syscalls | Bloqueo; wake desde idle poll |
| Timer | Quantum PIT (preemption diferida) |
| Arch | First switch + ASM |

## 6. Hoja de ruta

- CFS fair real con ops propias.
- Preemption por quantum de timer.
- SMP — no implementado.
