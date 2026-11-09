# Planificador de IR0

> **Última verificación:** 2026-11-07
> **Fuente de verdad:** `includes/ir0/sched.h`, `sched/sched.c`, `sched/backend.h`, `sched/backend_select.c`, `sched/rr/backend.c`, `sched/prio/backend.c`

| Campo | Valor |
|-------|-------|
| Versión | 0.3 |
| Fase IR0 | T0 |
| Estado | stable |
| Depende de | process, memory |
| Página man | IR0-scheduler (sección 7) |
| Fuentes principales | `includes/ir0/sched.h`, `sched/sched.c`, `sched/backend_select.c`, `sched/rr/backend.c`, `sched/prio/backend.c`, `sched/sched_switch.c`, `arch/common/arch_portable.h`, `sched/switch/switch_x64.asm`, `drivers/timer/clock_system.c` |

## 1. Visión general

IR0 planifica procesos mediante una **fachada portable** y una **tabla de ops**
seleccionada por `CONFIG_SCHEDULER_POLICY`. El defconfig usa **bandas de
prioridad** (`=2`). Round-robin (`0`) y el alias de nombre CFS (`1`) siguen
disponibles. Planificación **un CPU**; preemption por timer diferida.

La primera transferencia pasa por `first_switch_to(next)`.
El código portable **no** embebe `iretq`.

## 2. Arquitectura interna

| Pieza | Rol |
|-------|-----|
| `includes/ir0/sched.h` | API portable (sin `<sched/…>`) |
| `sched/sched.c` | Fachada portable; despacho vía `sched_backend_active()` |
| `sched/backend.h` | Contrato privado de ops |
| `sched/backend_select.c` | Selección de política en compilación |
| `sched/rr/backend.c` / `sched/prio/backend.c` | Implementaciones; una tabla por política vía su propio `backend.h` |
| `sched/sched_switch.c` | `sched_context_switch_to` compartido |
| `sched/sched_resched.c` | Helpers de resched (IRQ/TTY/retorno a user) |
| `sched/prio/backend.c` | Backend por defecto (policy 2) |
| `sched/rr/backend.c` | Lista de listos (policy 0 / 1) |
| `first_switch_to` | Primera entrada (propiedad de arch) |

**Policy `1` (`cfs`):** mismo objeto backend que RR; no existe `cfs_sched.c`
ni un wrapper que incluya el runqueue de RR.

## 3. Flujo

`schedule()` → ops del backend → pick → `sched_context_switch_to` →
`first_switch_to` / `switch_to`.

## 4. Límites del subsistema

- Callers → solo `<ir0/sched.h>`.
- Un backend no incluye el runqueue de otro.
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
