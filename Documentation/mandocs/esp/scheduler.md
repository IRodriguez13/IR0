# Planificador de IR0

| Campo | Valor |
|-------|-------|
| Versión | 0.2 |
| Fase IR0 | T0 |
| Estado | stable |
| Depende de | process, memory |
| Página man | IR0-scheduler (sección 7) |
| Fuentes principales | `sched/scheduler_api.c`, `sched/priority_sched.c`, `sched/rr_sched.c`, `arch/common/arch_portable.h` (`arch_first_context_switch`), `sched/switch/switch_x64.asm`, `drivers/timer/clock_system.c` |

## 1. Visión general

IR0 planifica procesos ejecutables mediante una fachada de política seleccionada
por config. El defconfig por defecto usa **bandas de prioridad**
(`CONFIG_SCHEDULER_POLICY=2`). Round-robin (`0`) y CFS-alias-RR (`1`) siguen
disponibles. La planificación está orientada a **un solo CPU**; la preemption
por timer está deshabilitada en el manejador PIT.

La primera transferencia a una tarea recién runnable pasa por
`arch_first_context_switch(next)` (x86: `user_mode.c`; ARM: `first_switch.c`).
El código portable RR/prioridad **no** debe embeber `iretq` / frames de entrada ISA.

## 2. Arquitectura interna

| Pieza | Rol |
|-------|-----|
| `scheduler_api.c` | Despacho al backend de política activo |
| `priority_sched.c` | Bandas por defecto con `CONFIG_SCHEDULER_POLICY=2` |
| `rr_sched.c` | Cola circular FIFO; helpers de pick compartidos |
| `rr_task_t` | `{ process_t *process; rr_task_t *next }` |
| `process_t::state` | `READY`, `RUNNING`, `BLOCKED`, `ZOMBIE` |
| `task_t` | Contexto visible para ASM (CR3, RIP, RSP, segmentos) en offset 0 de `process_t` |
| `arch_first_context_switch` | Primera entrada user/kernel (propiedad de arch) |
| `arch_context_switch.c` | Switches posteriores → `switch_context_x64` / ruta ARM |
| `switch_x64.asm` | Guarda GPRs, CR3, frame user `iretq` |

## 3. Flujo de datos

**`sched_schedule_next()` (backend de política):**

1. CLI (seguro ante IRQ).
2. Si la cola está vacía → retorno (el bucle idle maneja HLT).
3. Elige siguiente runnable; salta `ZOMBIE` y `BLOCKED`.
4. Marca prev `RUNNING→READY`, next `READY→RUNNING`; `current_process = next`.
5. Primer switch: carga espacio de direcciones; llama `arch_first_context_switch(next)`.
6. Posteriores: `arch_context_switch(&prev->task, &next->task)`.

**Rutas de wake (ponen `PROCESS_READY`, pueden llamar `sched_schedule_next`):**

```text
  syscall bloqueada ──► PROCESS_BLOCKED
                           │
  poll_wake_check ─────────┤ idle: kernel_idle_poll()
  sleep_wake_check ────────┤
  stdin_wake_check ────────┤ IRQ teclado
  pipe_wake_* ─────────────┤
  IPC wake ────────────────┘
                           ▼
                    PROCESS_READY → sched_schedule_next
```

**Timer (`clock_system.c`):** el tick PIT incrementa el contador de quantum;
**`sched_schedule_next` en IRQ está en `#if 0`** — preemption diferida hasta
endurecer iretq.

## 4. Responsabilidades

- El planificador elige la siguiente tarea runnable; no implementa el bloqueo de syscalls.
- Los procesos bloqueados permanecen fuera de la cola hasta wake → `PROCESS_READY`.
- La tarea idle del kernel (`comm "idle"`) se reencola al salir del user; se mantiene fuera mientras corre PID1 (ver comentarios en `process.c`).
- Arch posee los detalles ISA de primera entrada y context switch.

## 5. Límites del subsistema

- No hacer `HLT` dentro de schedule-next (IF=0 en contexto IRQ).
- El ASM de context switch posee el layout de `task_t`; cambiar offsets exige actualizar ASM.
- Código portable llama `sched_schedule_next()` vía `includes/ir0/scheduler_api.h`.
- Sin `iretq` inline en `rr_sched.c` / backends de prioridad.

## 6. Relaciones con otros subsistemas

| Vecino | Interacción |
|--------|-------------|
| Process | `sched_add_process` / `sched_remove_process` en spawn/exit |
| Syscalls | Bloqueo en handler; wake desde idle poll |
| Timer | Conteo de quantum PIT (hook de preemption deshabilitado) |
| Arch | `arch_first_context_switch`, `switch_context_x64` / ARM |

## 7. Mapas visuales

```text
       ┌─────────────┐
       │ cola run    │◄── sched_add_process
       └──────┬──────┘
              │ sched_schedule_next
              ▼
       ┌─────────────┐     ┌──────────────────────────┐
       │ RUNNING     │────►│ arch_first_context_switch│
       └─────────────┘     │ / arch_context_switch    │
              ▲            └──────────────────────────┘
              │ wake
       ┌──────┴──────┐
       │ BLOCKED     │
       └─────────────┘
```

Transiciones de estado:

```text
  READY ──pick──► RUNNING ──block──► BLOCKED
    ▲                  │                │
    └──── wake ────────┘                │
    ▲                                     │
    └─────────── wake ────────────────────┘
  RUNNING ──exit──► ZOMBIE (omitido por el picker)
```

## 8. Invariantes importantes

1. `process_t` comienza con `task_t` embebido en offset 0.
2. `sched_add_process` duplicado es idempotente (marca READY, libera nodo duplicado).
3. Las tareas zombie se omiten al elegir siguiente.
4. La IRQ de timer no debe preemptar hasta habilitar y probar el bloque `#if 0`.
5. La primera entrada siempre pasa por la fachada arch (sin iretq portable).

## 9. Consejos de depuración

Tags: `[FASE50][SCHED]`, `[FASE50][CTX]`, `[WAIT_EXIT_AUDIT]` (`IR0_DEBUG_WAIT`).

- Si el sistema cuelga con tareas ejecutables: comprobar que todas están atascadas en `BLOCKED`.
- Si no hay preemption: esperado — ruta timer deshabilitada; solo `sched_schedule_next` explícito.
- Confirmar política vía API del scheduler (`"priority"` cuando policy=2).
- ktest/host: tests relacionados bajo `kernel/test/` cuando estén habilitados.

## 10. Hoja de ruta futura

- Re-habilitar preemption por quantum de timer en `clock_tick_handler`.
- Colas de ejecución SMP y `current_process` por CPU — **no implementado**.
- CFS fair real (hoy policy 1 es alias de RR).
- Tradeoff idle work-conserving vs HLT en `kernel_idle_poll`.
