# x86-64 interrupt backend

> **Last verified:** 2026-11-04
> **Portable entry:** `#include <ir0/irq.h>`

This tree is the **x86-64** IDT / PIC / ISR / keyboard backend, split across:

- `arch/x86-64/sources/interrupt/` — C implementation
- `arch/x86-64/include/interrupt/` — backend headers
- `arch/x86-64/asm/interrupt/` — ISR assembly stubs

It is linked only when `ARCH=x86-64` (`INTERRUPT_OBJS_X86_64` in the top-level
Makefile).

| ISA | Bring-up path |
|-----|----------------|
| x86-64 | `arch/x86-64/sources/irq.c` → this tree |
| arm64 | `arch/arm64/sources/irq.c` → VBAR + `gic_v2` (never links this tree) |

Do **not** include these backend headers from `kernel/`, `fs/`, `mm/`, `net/`,
`drivers/`, or `sched/`. Use `ir0/irq.h` and `ir0/arch_io.h`.
