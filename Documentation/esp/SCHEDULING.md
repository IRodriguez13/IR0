# Scheduling en IR0

> **Última verificación:** 2026-07-17
> **Fuente de verdad:** `includes/ir0/sched.h`, `sched/sched.c`, `sched/rr_sched.c`, `sched/priority_sched.c`

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

## Características runtime

- Integración con procesos y señales.
- Mutaciones de cola serializadas por backend.
- Context-switch assembly específico por arquitectura.

## Puntos fuertes

- Callers portables no incluyen headers de RR ni de peers.
- Priority es backend real (no wrapper que incluye `rr_sched.h`).

## Puntos débiles

- Policy `1` aún no es CFS fair real.
- SMP no es el baseline.
- Preemption por timer en IRQ sigue diferida (ver mandocs).

## Relacionado

- Mandocs: [`../mandocs/esp/scheduler.md`](../mandocs/esp/scheduler.md)
- Desacoplamiento: [`../DECOUPLING.md`](../DECOUPLING.md)
