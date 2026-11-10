# Scheduling en IR0

> **Última verificación:** 2026-11-09
> **Fuente de verdad:** `includes/ir0/sched.h`, `sched/core.h`, `sched/core.c`,
> `sched/rr.c`, `sched/prio.c`, `kernel/time/clock_wait.c`
> **Canónico (inglés):** [`../SCHEDULING.md`](../SCHEDULING.md)

El scheduling en IR0 es un núcleo más un archivo de política elegido en
compilación desde Kconfig. No hay despacho en tiempo de ejecución.

## Modelo de selección

- API portable: [`includes/ir0/sched.h`](../../includes/ir0/sched.h).
- Núcleo: [`sched/core.c`](../../sched/core.c) (`schedule()`, cambio de contexto,
  `sched_take_resched()`).
- Contrato privado: [`sched/core.h`](../../sched/core.h). La política implementa
  las operaciones de cola de `<ir0/sched.h>` y `sched_pick_next()`.
- Lock de runqueue: `rq_lock()` / `rq_unlock()`; en UP enmascaran IRQs. Todo lo
  multi-CPU (arranque de CPUs, IPIs, áreas per-CPU, llamadas cruzadas, balanceo)
  va en `smp/`, que implementará ese par sin tocar `sched/`.

### Políticas

| `CONFIG_SCHEDULER_POLICY` | Nombre | Objeto |
|---------------------------|--------|--------|
| `0` | `round_robin` | `sched/rr.o` |
| `1` | `cfs` | Mismo objeto RR (alias honesto, sin `cfs_sched.c`) |
| `2` (defconfig) | `priority` | `sched/prio.o` |

## Syscalls bloqueantes y señales

Una syscall bloqueante duerme en su propia pila de kernel (`PROCESS_BLOCKED` +
`schedule()`); si `signal_pending()`, devuelve `-ERESTARTSYS` / `-EINTR`, y la
salida de syscall (`exit_to_user_mode` → `do_signal`) reinicia la llamada o
entra al handler a través del frame de la syscall. Detalle: canónico EN.

## Class B

Par CS ring-0 + RIP userspace ilegal para `kernel_ret`. Producto:
`IR0_CLASS_B_REPAIR=1`. Gates actuales: escenario KTM Class B (manual) y
`make -C tests/host run` (`test_class_b_ctx_invariant`). Los targets
`smoke-class-b-*` se eliminaron con el catálogo QA. Ver [`../KTM.md`](../KTM.md).

## Características runtime

- Integración con procesos y señales.
- Mutaciones de cola serializadas por backend.
- Context-switch assembly específico por arquitectura.

## Puntos fuertes

- Callers portables no incluyen headers de RR ni de peers.
- Priority es backend real (no wrapper que incluye el runqueue de RR).
- Sleeps de poll/pause ceden sin tormentas de schedule anidadas.

## Puntos débiles

- Policy `1` aún no es CFS fair real.
- SMP no es el baseline actual.

## Relacionado

- Mandocs: [`../mandocs/esp/scheduler.md`](../mandocs/esp/scheduler.md)
- EN: [`../SCHEDULING.md`](../SCHEDULING.md)
