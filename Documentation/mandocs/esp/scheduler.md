# Planificador de IR0

> **Última verificación:** 2026-07-17
> **Fuente de verdad:** `includes/ir0/sched.h`, `sched/sched.c`, `sched/sched_ops.h`, `sched/rr_sched.c`, `sched/priority_sched.c`

| Campo | Valor |
|-------|-------|
| Versión | 0.3 |
| Fase IR0 | T0 |
| Estado | stable |
| Depende de | process, memory |
| Página man | IR0-scheduler (sección 7) |
| Fuentes principales | `includes/ir0/sched.h`, `sched/sched.c`, `sched/sched_switch.c`, `sched/priority_sched.c`, `sched/rr_sched.c`, `arch/common/arch_portable.h`, `sched/switch/switch_x64.asm`, `drivers/timer/clock_system.c` |

## 1. Visión general

IR0 planifica procesos mediante una **fachada portable** y una **tabla de ops**
seleccionada por `CONFIG_SCHEDULER_POLICY`. El defconfig usa **bandas de
prioridad** (`=2`). Round-robin (`0`) y el alias de nombre CFS (`1`) siguen
disponibles. Planificación **un CPU**; preemption por timer diferida.

La primera transferencia pasa por `arch_first_context_switch(next)`.
El código portable **no** embebe `iretq`.

## 2. Arquitectura interna

| Pieza | Rol |
|-------|-----|
| `includes/ir0/sched.h` | API portable (sin `<sched/…>`) |
| `sched/sched.c` | Ops activas; despacho |
| `sched/sched_ops.h` | Contrato de backend |
| `sched/sched_switch.c` | `sched_context_switch_to` compartido |
| `sched/sched_resched.c` | Helpers de resched (IRQ/TTY/retorno a user) |
| `priority_sched.c` | Backend por defecto (policy 2) |
| `rr_sched.c` | Cola circular (policy 0 / 1) |
| `arch_first_context_switch` | Primera entrada (propiedad de arch) |

**Policy `1` (`cfs`):** mismas ops que RR; no existe `cfs_sched.c` ni un wrapper
que incluya `rr_sched.h`.

## 3. Flujo

`sched_schedule_next()` → ops del backend → pick → `sched_context_switch_to` →
`arch_first_context_switch` / `arch_context_switch`.

## 4. Límites del subsistema

- Callers → solo `<ir0/sched.h>`.
- Un backend no incluye el runqueue de otro.
- `scheduler_api.h` es alias de compatibilidad.
- Sin `iretq` inline en código sched portable.

## 5. Relaciones

| Vecino | Interacción |
|--------|-------------|
| Process | `sched_add_process` / `sched_remove_process` |
| Syscalls | Bloqueo; wake desde idle poll |
| Timer | Quantum PIT (preemption diferida) |
| Arch | First switch + ASM |

## 6. Hoja de ruta

- CFS fair real con ops propias.
- Preemption por quantum de timer.
- SMP — no implementado.
