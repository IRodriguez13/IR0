# Planificador de IR0

| Campo | Valor |
|-------|-------|
| Versión | 0.1 |
| Fase IR0 | T0 |
| Estado | stable |
| Depende de | process, memory |
| Página man | IR0-scheduler (sección 7) |
| Fuentes principales | `sched/scheduler_api.c`, `sched/rr_sched.c`, `sched/switch/switch_x64.asm`, `drivers/timer/clock_system.c`, `kernel/main.c` |

## 1. Visión general

IR0 planifica procesos ejecutables mediante una fachada de política seleccionada
por config. El build por defecto usa **round-robin** (`CONFIG_SCHEDULER_POLICY=0`).
Los backends CFS y por prioridad compilan pero no son el predeterminado. La
planificación está orientada a **un solo CPU**; la preemption impulsada por timer
está deshabilitada actualmente en el manejador PIT.

## 2. Arquitectura interna

| Pieza | Rol |
|-------|-----|
| `scheduler_api.c` | Despacho al backend de política activo |
| `rr_sched.c` | Cola circular FIFO (`rr_head`, `rr_current`) |
| `rr_task_t` | `{ process_t *process; rr_task_t *next }` |
| `process_t::state` | `READY`, `RUNNING`, `BLOCKED`, `ZOMBIE` |
| `task_t` | Contexto visible para ASM (CR3, RIP, RSP, segmentos) en offset 0 de `process_t` |
| `arch_context_switch.c` | Elige `switch_context_x64` vs ruta resume-syscall |
| `switch_x64.asm` | Guarda GPRs, CR3, frame user `iretq` |

## 3. Flujo de datos

**`sched_schedule_next()` (RR):**

1. CLI (seguro ante IRQ).
2. Si la cola está vacía → retorno (el bucle idle maneja HLT).
3. Avanza `rr_current` circularmente; salta `ZOMBIE` y `BLOCKED` (máx. 100 intentos).
4. Marca prev `RUNNING→READY`, next `READY→RUNNING`; `current_process = next`.
5. Primer switch: carga CR3; user → `arch_switch_to_user`; tarea kernel → iretq inline.
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

**Timer (`clock_system.c`):** el tick PIT incrementa contador de quantum; **`sched_schedule_next` en IRQ está `#if 0`** — preemption diferida hasta completar endurecimiento iretq.

## 4. Responsabilidades

- El scheduler elige la siguiente tarea ejecutable; no implementa bloqueo en syscall.
- Los procesos bloqueados permanecen fuera de la cola RR hasta que wake pone `PROCESS_READY`.
- La tarea idle del kernel (`comm "idle"`) se reencola al salir user; se mantiene fuera de cola mientras corre PID1 (ver comentarios en `process.c`).

## 5. Límites del subsistema

- No debe hacer `HLT` dentro de `rr_schedule_next` (IF=0 en contexto IRQ).
- El ASM de context switch posee el layout de `task_t`; cambiar offsets exige actualizar ASM.
- El código portable llama `sched_schedule_next()` vía `includes/ir0/scheduler_api.h`.

## 6. Relaciones con otros subsistemas

| Vecino | Interacción |
|--------|-------------|
| Process | `sched_add_process` / `sched_remove_process` en spawn/exit |
| Syscalls | Bloqueo en handler; wake desde idle poll |
| Timer | Conteo de quantum PIT (hook de preemption deshabilitado) |
| Arch | `switch_context_x64`, `arch_switch_to_user` |

## 7. Mapas visuales

```text
       ┌─────────────┐
       │ cola RR     │◄── sched_add_process
       └──────┬──────┘
              │ sched_schedule_next
              ▼
       ┌─────────────┐     ┌──────────────────┐
       │ RUNNING     │────►│ switch_context   │
       └─────────────┘     │ _x64 / sysret    │
              ▲            └──────────────────┘
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
  RUNNING ──exit──► ZOMBIE (omitido por RR)
```

## 8. Invariantes importantes

1. `process_t` comienza con `task_t` embebido en offset 0.
2. `sched_add_process` duplicado es idempotente (marca READY, libera nodo duplicado).
3. La eliminación RR es recorrido O(n) de lista enlazada.
4. Las tareas zombie se omiten al elegir la siguiente.
5. La IRQ de timer no debe preemptar hasta habilitar y probar el bloque `#if 0`.

## 9. Consejos de depuración

Tags: `[FASE50][SCHED]`, `[FASE50][CTX]`, `[WAIT_EXIT_AUDIT]` (`IR0_DEBUG_WAIT`).

- Si el sistema cuelga con tareas ejecutables: comprobar que todas están atascadas en `BLOCKED`.
- Si no hay preemption: esperado — ruta timer deshabilitada; solo `sched_schedule_next` explícito.
- ktest/host: tests relacionados con scheduler bajo `kernel/test/` cuando están habilitados.

## 10. Hoja de ruta futura

- Re-habilitar preemption por quantum de timer en `clock_tick_handler`.
- Colas de ejecución SMP y `current_process` por CPU — **no implementado**.
- Políticas CFS/prioridad necesitan paridad de funcionalidad antes de cambio por defecto.
- Tradeoff idle work-conserving vs HLT en `kernel_idle_poll`.
