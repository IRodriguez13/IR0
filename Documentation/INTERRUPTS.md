# IR0 Interrupt and Exception Path

IR0 uses x86-64 IDT + PIC flow with a syscall gateway and exception-to-signal
integration for process-facing error handling.

## Core Components

- IDT setup and load path in `interrupt/arch/idt.c`.
- PIC setup and IRQ masking/unmasking in `interrupt/arch/pic.c`.
- ISR stubs in architecture assembly.
- C-side routing and handlers in `interrupt/arch/isr_handlers.c`.

## Runtime Behavior

- CPU exceptions are routed through ISR handling and mapped to process signals.
- Hardware IRQs are acknowledged with EOI after handler dispatch.
- Syscalls enter through the syscall path and dispatch table in kernel syscall code.

## Notable Characteristics

- Legacy PIC model remains active.
- Exception handling favors system continuity when possible.
- Signal delivery is integrated with process/scheduler flow.

## Strengths

- Stable and understandable interrupt bring-up path.
- Good debug visibility through serial logs and `/proc/interrupts`.
- Signal mapping reduces full-system collapse for user-space faults.

## Weak Points

- APIC/SMP-oriented interrupt scaling is not the current default path.
- Advanced interrupt threading and fine-grained prioritization are limited.

