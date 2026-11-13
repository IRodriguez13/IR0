# IR0 Interrupts and Exceptions

| Field | Value |
|-------|-------|
| Version | 0.1 |
| IR0 phase | T0 |
| Status | stable |
| Depends on | boot, drivers, syscalls |
| Man page | IR0-interrupts (section 7) |
| Primary sources | `arch/x86-64/sources/interrupt/{idt,pic,isr_handlers}.c`, `arch/x86-64/asm/interrupt/isr_stubs_64.asm`, `arch/x86-64/asm/syscall_*.asm`, `arch/x86-64/sources/fault.c` |

## 1. Overview

x86-64 interrupt handling uses a 256-entry IDT, dual 8259 PIC (IRQ 0–15 → vectors
32–47), assembly stubs, and C handlers in `isr_handler64`. Syscalls enter via
the **`syscall` insn** only (musl/Linux ABI); `int $0x80` is `#GP`. CPU
exceptions map to signals for user tasks or panic for kernel faults.

## 2. Internal architecture

| Component | Path |
|-----------|------|
| IDT | `arch/x86-64/sources/interrupt/idt.c` — 256 gates, `#DF` uses IST1 |
| PIC | `arch/x86-64/sources/interrupt/pic.c` — remap bases 0x20/0x28, mask/unmask, EOI |
| Stubs | `isr_stubs_64.asm` → `isr_common_stub_64` → `isr_handler64` |
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
- PIC: all IRQs masked at init; `ir0_boot_port_unmask_irqs` enables timer, keyboard, cascade, mouse, NIC.
- ISR: user exceptions → signals; kernel exceptions → panic (except #PF/#GP audit paths).
- Syscall entry: capture user frame for fork/signal resume on x86-64.

## 5. Subsystem boundaries

- Portable code must not arch interrupt backend headers — use facades and arch_port.
- Vector 0x80 has the kernel-only default gate: `int $0x80` from userspace is `#GP`.
- No APIC/IOAPIC primary path — legacy PIC only.

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|---------------|
| Timer | IRQ0 → PIT → scheduler quantum; preemption on the return to userspace |
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
  exc   IRQ0  IRQ1     syscall insn
   │     │     │          │
   ▼     ▼     ▼          ▼
 fault  PIT  kbd      syscall_dispatch
```

## 8. Important invariants

1. User-callable gate: `int3` only (DPL=3 on vector 3); syscalls use the `syscall` insn.
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
