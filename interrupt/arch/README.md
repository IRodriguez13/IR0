# interrupt/arch — x86-64 IRQ implementation

> **Last verified:** 2026-07-26  
> **Portable entry:** `#include <ir0/irq.h>`

This tree is the **x86-64** IDT / PIC / ISR / keyboard backend. It is linked only
when `ARCH=x86-64` (`INTERRUPT_OBJS_X86_64` in the top-level Makefile).

| ISA | Bring-up path |
|-----|----------------|
| x86-64 | `arch/x86-64/sources/arch_irq_init.c` → this tree |
| arm64 | `arch/arm64/sources/arch_irq_init.c` → VBAR + `gic_v2` (never links this tree) |

Do **not** `#include <interrupt/arch/...>` from `kernel/`, `fs/`, `mm/`, `net/`,
`drivers/`, or `sched/`. Use `ir0/irq.h` and `ir0/arch_io.h`.
