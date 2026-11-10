<!-- IR0 AI dev rule: ir0-api-public-private -->
<!-- alwaysApply: true -->
<!-- description: IR0 public vs private kernel APIs, simple polymorphic names, backend anti-coupling -->

# IR0 — API pública / privada y anti-acoplamiento

Complementa `kernel-architecture-rigor.md` y el facade rule de `ir0-userspace-monolith-debt.md`.

## Tres capas

| Capa | Dónde | Nombre | Quién consume |
|------|--------|--------|---------------|
| **Pública (facade)** | `includes/ir0/*.h` | **Simple y estable** (`switch_to`, `copy_from_user`, `cpu_relax`, `load_gdt`) | Cualquier subsistema portable |
| **API de subsistema** | header del módulo o `includes/ir0/<subsys>.h` acotado | Puede ser más verbosa (`posix_shm_try_unlink`) | Solo el glue de ese dominio — **no** backends rivales |
| **Privada** | `static`, `.c` local, `sched/*.h`, `arch/<isa>/` | Libre; sufijos `_x64` / `_arm64` OK | Solo el propio backend / ISA |

Fuera de implementación: consumir APIs públicas solo vía `#include <ir0/...>`.

## Naming de facades polimórficas (política de todo el kernel)

Si la función **por detrás** usa asm/intrinsics de ISA, el nombre público debe ser **simple**: sin prefijo `arch_`, sin sufijo de ISA, sin “load_x86_…”. El build (`ARCH_*`) elige la implementación.

```text
❌ BAD:  arch_context_switch(), arch_load_x86_gdt(), arch_cpu_relax()
✅ GOOD: switch_to(), load_gdt(), cpu_relax()
```

- **Prohibido** en API **nueva**: prefijo `arch_` para hot paths portables.
- Legacy `arch_*` (`arch_irq_save`, `arch_mm_activate`, …): no expandir; al tocar call sites, preferir alias simple (`enable_interrupts`, `cpu_halt`, `timer_read`) o rename en oleada dedicada.
- Símbolos ISA privados solo en dispatcher / `arch/<isa>/`.

## Anti-acoplamiento (caso sched)

Las políticas (RR, priority, futuras) **no** incluyen ni llaman privados de otra. `CONFIG_SCHEDULER_POLICY` enlaza **un** archivo de política (`sched/rr.c` o `sched/prio.c`) que implementa el contrato fijo de `sched/core.h` con funciones planas; nada de tabla de ops en runtime para una elección de compilación. La política elige `next`; el núcleo (`sched/core.c`) cambia de contexto.

```text
❌ BAD:  sched/prio.c → #include "rr.h"; struct *_ops con un solo backend enlazado
✅ GOOD: schedule() → sched_pick_next() (política) → sched_switch_to(next) → switch_to(prev, next)
```

Todo lo que solo existe con varias CPUs (arranque de CPUs, IPIs, áreas per-CPU, llamadas cruzadas, balanceo de carga) vive en `smp/`. El núcleo del scheduler usa solo `rq_lock()` / `rq_unlock()`; hoy (UP) es `irq_save`/`irq_restore` en `sched/core.h` y `smp/` lo implementará.

## Polimorfismo ISA — todo camino con asm específico

**Regla dura:** toda función cuya implementación use asm/intrinsics de ISA debe ser polimórfica (impl por cada ISA del árbol) y exponerse con **nombre simple** en `includes/ir0/`.

| Capa | Obligación |
|------|------------|
| **Facade** | Nombre simple (`switch_to`, `cpu_relax`, `smp_mb`, `inb`, …). Portable solo llama esto. |
| **Dispatcher** | Elige impl por `ARCH_*` / Kconfig. |
| **Impl ISA** | Asm/C bajo `arch/<isa>/` o `sched/switch/switch_<isa>.*`. |

### Prohibido en código portable

- Labels de una sola ISA (`switch_context_x64`, …).
- `__asm__` / inline asm ISA en `kernel/`, `fs/`, `mm/`, `net/`, `sched` backends, `drivers/`, `includes/ir0/*.c` (salvo barrier vacío `asm volatile("" ::: "memory")`).
- Hot path “solo x86” sin facade + stub/impl para las demás ISA.

Enforcement: `scripts/architecture_guard.py` → `check_portable_no_isa_asm`.

### Context switch (canónico)

Modelo Linux: cada tarea duerme en su propia pila de kernel y `switch_to()` vuelve cuando la tarea corre otra vez; se vuelve a userspace solo por la salida normal de syscall/excepción (o, en la primera corrida, por `switch_to_user_task`, como `ret_from_fork`). Nada de reparchear frames de usuario para "reanudar" syscalls.

- Pública (`includes/ir0/context.h`): `switch_to(prev, next)` (prev == NULL: primera transferencia, no vuelve), `switch_to_user_task`, `switch_to_user`.
- Sched interna: `schedule()` → `sched_switch_to(next)` (`sched/core.c`) → `switch_to`.
- Hooks de arquitectura (`includes/ir0/context_backend.h`, solo `sched/switch/` y `arch/`): `__switch_to`, `first_switch_to` y `set_current_kernel_stack`.
- ISA privada: `switch_context_x64` / `switch_context_arm64` solo detrás de `__switch_to`.
- Sin `#if` ni nombres de registros en `sched/switch/`; los diagnósticos van por `switch_trace()` (`sched/switch/diag.c`).

## Operations validate themselves (minimal variants)

- The operation validates internally; callers do not pre-validate.
  `copy_{to,from}_user`, `strncpy_from_user` and the `_mm` variants walk
  the page tables and return `-EFAULT`: a syscall copies and returns that
  error, it does not call `access_ok()` first.
- At most one public predicate per contract, for the cases where nothing is
  copied (Linux `access_ok`). No `*_mapped`, `*_checked` or similar parallel
  public functions for one behavior.
- Where a syscall consumes or changes state before it copies out (socket,
  pipe or file reads), restructure it Linux-style: copy first, consume only
  what reached userspace, instead of validating ahead.
- Retired names are rejected by `scripts/architecture_guard.py`
  (`[uaccess-single-api]` lists them) so they do not come back.

## Excepciones

- Bring-up freestanding (`arch/arm64/sources/board_boot_min.c`, etc.): asm/backend directo OK si el file header dice **no producción**.

## Al añadir API

1. ¿≥2 subsistemas o contrato estable? → facade `includes/ir0/`, **nombre simple**.
2. ¿Solo glue de un dominio? → API de subsistema; nombres descriptivos OK.
3. ¿Solo un `.c`? → privada.
4. Dos backends → `*_ops` + helper compartido; nunca include peer-to-peer.
5. ¿Cuerpo con asm ISA? → facade polimórfica de nombre simple + impl por cada arch.
