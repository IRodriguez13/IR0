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

Backends (RR, priority, futuros) **no** incluyen ni llaman privados del peer. Eligen `next` → `sched_context_switch_to` → `switch_to`. Ops table / facade; no includes cruzados.

```text
❌ BAD:  priority_sched.c → #include "rr_sched.h"
✅ GOOD: priority → sched_context_switch_to(next) → switch_to(prev, next)
```

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

- Pública: `switch_to` en `includes/ir0/context.h`.
- Sched interna: `sched_context_switch_to` → `switch_to`.
- ISA privada: `switch_context_x64` / `switch_context_arm64` solo en el dispatcher.
- Entrada ring 3: `switch_to_user` / `switch_to_user_task` — mismo polimorfismo, otro contrato; no mezclar con `switch_to`.

## Excepciones

- Bring-up freestanding (`arch/arm64/sources/rr_early.c`, etc.): asm/backend directo OK si el file header dice **no producción**.
- Shared `sched_switch.*` / `*_ops` entre backends: OK; no runqueues cruzadas.

## Al añadir API

1. ¿≥2 subsistemas o contrato estable? → facade `includes/ir0/`, **nombre simple**.
2. ¿Solo glue de un dominio? → API de subsistema; nombres descriptivos OK.
3. ¿Solo un `.c`? → privada.
4. Dos backends → `*_ops` + helper compartido; nunca include peer-to-peer.
5. ¿Cuerpo con asm ISA? → facade polimórfica de nombre simple + impl por cada arch.
