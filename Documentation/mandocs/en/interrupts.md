# IR0 Interrupts and Exceptions

| Field | Value |
|-------|-------|
| Version | 0.1 |
| IR0 phase | T0 |
| Status | stable |
| Depends on | boot, drivers, syscalls |
| Man page | IR0-interrupts (section 7) |
| Primary sources | `interrupt/arch/{idt,pic,isr_handlers}.c`, `interrupt/arch/x86-64/isr_stubs_64.asm`, `arch/x86-64/asm/syscall_*.asm`, `arch/x86-64/sources/fault.c` |

## 1. Overview

x86-64 interrupt handling uses a 256-entry IDT, dual 8259 PIC (IRQ 0–15 → vectors
32–47), assembly stubs, and C handlers in `isr_handler64`. Syscalls enter via
**int 0x80** (debug_bins ABI) and **`syscall` insn** (musl/Linux ABI). CPU
exceptions map to signals for user tasks or panic for kernel faults.

## 2. Internal architecture

| Component | Path |
|-----------|------|
| IDT | `interrupt/arch/idt.c` — 256 gates, `#DF` uses IST1 |
| PIC | `interrupt/arch/pic.c` — remap bases 0x20/0x28, mask/unmask, EOI |
| Stubs | `isr_stubs_64.asm` → `isr_common_stub_64` → `isr_handler64` |
| Syscall int | `syscall_entry_64.asm` — IDT 0x80 / vector 128 jmp to entry |
| Syscall insn | `syscall_insn_entry_64.asm` — LSTAR, 8 KiB kstack |
| Page fault | `fault.c` — demand-fill or SIGSEGV |

## 3. Data flow

**Hardware IRQ:**

```text
  IRQ line ──► IDT vector 32+irq
       ──► isr_stub (cli, save regs)
       ──► isr_handler64(vector, frame)
       ──► device handler (timer/keyboard/mouse/net)
       ──► pic_send_eoi64
       ──► iretq
```

**int 0x80 syscall:**

```text
  int $0x80 ──► syscall_entry_asm
       ──► map debug_bins regs → Linux C ABI
       ──► syscall_dispatch()
       ──► iretq (user segments 0x23)
```

**Handled IRQs in switch:**

```text
  IRQ0  (32) PIT + clock_tick_handler
  IRQ1  (33) keyboard_handler64
  IRQ12 (44) input_mouse_handle_interrupt (if mouse enabled)
  NIC   net_stack_handle_irq (if CONFIG_ENABLE_NETWORKING)
  others: EOI only (no driver handler yet)
```

## 4. Responsibilities

- IDT: install gates before `sti`.
- PIC: all IRQs masked at init; `boot_irq_unmask` enables timer, keyboard, cascade, mouse, NIC.
- ISR: user exceptions → signals; kernel exceptions → panic (except #PF/#GP audit paths).
- Syscall entry: capture user frame for fork/signal resume on x86-64.

## 5. Subsystem boundaries

- Portable code must not `#include <interrupt/arch/...>` — use facades and arch_port.
- Vector 128 handler in C is no-op; real int 0x80 jumps directly to syscall asm.
- No APIC/IOAPIC primary path — legacy PIC only.

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|---------------|
| Timer | IRQ0 → PIT → scheduler quantum (preempt `#if 0`) |
| TTY/Input | IRQ1 keyboard, IRQ12 mouse |
| Net | NIC IRQ → rtl8139 |
| Scheduler | IRQ context must not HLT |
| Signals | ISR sends signals for user CPU exceptions |

## 7. Visual maps

```text
        CPU
         │
    ┌────┴────┐
    │   IDT   │
    └────┬────┘
         │
   ┌─────┼─────┬──────────┐
   ▼     ▼     ▼          ▼
  exc   IRQ0  IRQ1    int 0x80 / syscall
   │     │     │          │
   ▼     ▼     ▼          ▼
 fault  PIT  kbd      syscall_dispatch
```

## 8. Important invariants

1. User-callable gate: syscall only (DPL=3 on vector 0x80).
2. HW exceptions with error code: vectors 8, 10–14, 17.
3. `#PF` user: demand-fill heap/mmap or SIGSEGV + exit.
4. `#PF` kernel: panic.
5. `CONFIG_DEBUG_ISRABI` audits iretq frame contract.

## 9. Debugging tips

- Tags: `[ISR]`, `[ISRABI]`, `[PF_AUDIT]`, `[GPF_AUDIT]`, `[BOOT] Interrupts enabled`.
- `/proc/interrupts` — counter snapshot.
- Most IRQ lines (COM, IDE) have stubs without handlers.

Legacy: `Documentation/INTERRUPTS.md`, `interrupt/README.md`.

## 10. Future roadmap

- APIC/LAPIC as primary interrupt controller — partial LAPIC timer code only.
- Handlers for ATA/COM IRQs — not wired in ISR switch.
- Recoverable kernel exceptions — mostly panic today.
- Unify redundant vector 128 path with int 0x80 entry.
