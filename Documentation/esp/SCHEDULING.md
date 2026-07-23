# Scheduling en IR0

> **Última verificación:** 2026-07-23
> **Fuente de verdad:** `includes/ir0/sched.h`, `sched/sched.c`, `sched/rr_sched.c`,
> `sched/priority_sched.c`, `sched/sched_switch.c`, `kernel/clock_wait.c`  
> **Canónico (inglés):** [`../SCHEDULING.md`](../SCHEDULING.md)

El scheduling en IR0 se selecciona por una fachada portable y una tabla de ops
de backend configurada desde Kconfig.

## Modelo de selección

- API portable: [`includes/ir0/sched.h`](../../includes/ir0/sched.h).
- Despacho: [`sched/sched.c`](../../sched/sched.c) con `struct ir0_sched_ops`
  según `CONFIG_SCHEDULER_POLICY`.
- Ops de backend: [`sched/sched_ops.h`](../../sched/sched_ops.h) — RR y priority
  exportan su propia tabla; un backend no incluye al otro.
- Switch compartido: [`sched/sched_switch.c`](../../sched/sched_switch.c).
- Compat: `scheduler_api.h` solo redirige a `<ir0/sched.h>`.

### Políticas

| `CONFIG_SCHEDULER_POLICY` | Nombre | Objeto |
|---------------------------|--------|--------|
| `0` | `round_robin` | `rr_sched.o` |
| `1` | `cfs` | Alias honesto de RR (sin `cfs_sched.c`) |
| `2` (defconfig) | `priority` | `priority_sched.o` |

## Syscalls bloqueantes (poll / pause / nanosleep)

Los waiters usan `process_arm_kernel_syscall_sleep` + `clock_wait`. En el idle
del wait se llama `kernel_idle_poll_nosched()` para que los wakes no aniden
`sched_schedule_next` (el loop del syscall es el único yield). Detalle: canónico EN.

## Class B

Par CS ring-0 + RIP userspace ilegal para `kernel_ret`. Producto:
`IR0_CLASS_B_REPAIR=1`. Gates: `make smoke-class-b-mitigated` /
`smoke-class-b-repro` (`scripts/make/class-b.mk`). Ver [`../KTM.md`](../KTM.md).

## Características runtime

- Integración con procesos y señales.
- Mutaciones de cola serializadas por backend.
- Context-switch assembly específico por arquitectura.

## Puntos fuertes

- Callers portables no incluyen headers de RR ni de peers.
- Priority es backend real (no wrapper que incluye `rr_sched.h`).
- Sleeps de poll/pause ceden sin tormentas de schedule anidadas.

## Puntos débiles

- Policy `1` aún no es CFS fair real.
- SMP no es el baseline actual.

## Relacionado

- Mandocs: [`../mandocs/esp/scheduler.md`](../mandocs/esp/scheduler.md)
- EN: [`../SCHEDULING.md`](../SCHEDULING.md)
