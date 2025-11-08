# Interrupt Subsystem

Interrupt handling, exception management, and hardware interrupt routing.

## Architecture

```
interrupt/
├── idt.c/h              - Interrupt Descriptor Table (common)
├── isr_handlers.c/h     - Interrupt Service Routines (common)
├── pic.c/h              - Programmable Interrupt Controller
└── arch/
    ├── x86-64/
    │   ├── isr_stubs_64.asm - Assembly interrupt stubs
    │   └── interrupt.asm    - Interrupt entry points
    └── x86-32/
        ├── isr_stubs_32.asm
        └── interrupt.asm
```

## Components

### IDT (Interrupt Descriptor Table)

The IDT maps interrupt vectors to handler functions:
- 256 entries (0-255)
- Entries 0-31: CPU exceptions
- Entries 32-47: Hardware interrupts (IRQs)
- Entry 128 (0x80): System call interface

### PIC (Programmable Interrupt Controller)

8259 PIC configuration:
- Master PIC: IRQ 0-7 → INT 32-39
- Slave PIC: IRQ 8-15 → INT 40-47
- Cascade on IRQ 2

### ISR (Interrupt Service Routines)

Assembly stubs that:
1. Save CPU state
2. Call C handler
3. Send EOI to PIC
4. Restore CPU state
5. Return from interrupt (iretq/iret)

## Interrupt Vectors

### CPU Exceptions (0-31)
```
0  - Divide by Zero
1  - Debug
2  - Non-Maskable Interrupt
3  - Breakpoint
4  - Overflow
5  - Bound Range Exceeded
6  - Invalid Opcode
7  - Device Not Available
8  - Double Fault
9  - Coprocessor Segment Overrun
10 - Invalid TSS
11 - Segment Not Present
12 - Stack Segment Fault
13 - General Protection Fault
14 - Page Fault
15 - Reserved
16 - x87 FPU Error
17 - Alignment Check
18 - Machine Check
19 - SIMD FPU Exception
20-31 - Reserved
```

### Hardware Interrupts (32-47)
```
32 (IRQ 0)  - PIT Timer
33 (IRQ 1)  - Keyboard
34 (IRQ 2)  - Cascade (Slave PIC)
35 (IRQ 3)  - COM2
36 (IRQ 4)  - COM1
37 (IRQ 5)  - LPT2
38 (IRQ 6)  - Floppy
39 (IRQ 7)  - LPT1
40 (IRQ 8)  - RTC
41 (IRQ 9)  - Free
42 (IRQ 10) - Free
43 (IRQ 11) - Free
44 (IRQ 12) - PS/2 Mouse
45 (IRQ 13) - FPU
46 (IRQ 14) - Primary ATA
47 (IRQ 15) - Secondary ATA
```

### System Call (128)
```
128 (0x80) - System call interface
```

## Public Interface

### Initialization
```c
void idt_init64(void);      /* Initialize IDT for x86-64 */
void idt_load64(void);      /* Load IDT register */
void pic_remap64(void);     /* Remap PIC to INT 32-47 */
```

### IRQ Control
```c
void pic_mask_irq(uint8_t irq);      /* Disable IRQ */
void pic_unmask_irq(uint8_t irq);    /* Enable IRQ */
void pic_send_eoi64(uint8_t irq);    /* Send End-Of-Interrupt */
```

### Handler Registration
```c
void idt_set_gate64(uint8_t num, uint64_t handler, 
                    uint16_t sel, uint8_t flags);
```

## Interrupt Handling Flow

### Hardware Interrupt (e.g., Timer)
```
1. Hardware raises IRQ line
2. PIC sends interrupt to CPU
3. CPU looks up handler in IDT
4. CPU switches to kernel stack (TSS.rsp0)
5. CPU pushes SS, RSP, RFLAGS, CS, RIP
6. CPU jumps to isr_stub_32 (assembly)
7. isr_stub_32 saves all registers
8. isr_stub_32 calls isr_handler64(32)
9. isr_handler64 processes interrupt
10. isr_handler64 sends EOI to PIC
11. isr_stub_32 restores registers
12. isr_stub_32 executes iretq
13. CPU returns to interrupted code
```

### Exception (e.g., Page Fault)
```
1. CPU detects fault condition
2. CPU looks up handler in IDT (entry 14)
3. CPU pushes error code
4. CPU switches to kernel stack
5. CPU pushes SS, RSP, RFLAGS, CS, RIP
6. CPU jumps to isr_stub_14
7. isr_stub_14 calls isr_handler64(14)
8. isr_handler64 handles page fault
9. Either: fix and return, or panic
```

### System Call
```
1. User code executes: int 0x80
2. CPU looks up handler in IDT (entry 128)
3. CPU switches to kernel stack
4. CPU jumps to syscall_entry_asm
5. syscall_entry_asm extracts arguments
6. syscall_entry_asm calls syscall_handler()
7. syscall_handler() dispatches to sys_*()
8. Return value placed in RAX
9. iretq returns to user mode
```

## IDT Entry Format (x86-64)

```
Bits 0-15:   Offset low (handler address bits 0-15)
Bits 16-31:  Segment selector (usually 0x08 for kernel code)
Bits 32-34:  IST (Interrupt Stack Table, usually 0)
Bits 35-39:  Reserved (must be 0)
Bits 40-43:  Type (0xE = interrupt gate, 0xF = trap gate)
Bit 44:      Zero
Bits 45-46:  DPL (Descriptor Privilege Level)
Bit 47:      Present (must be 1)
Bits 48-63:  Offset mid (handler address bits 16-31)
Bits 64-95:  Offset high (handler address bits 32-63)
Bits 96-127: Reserved (must be 0)
```

## Interrupt Flags

```c
#define IDT_INTERRUPT_GATE_KERNEL  0x8E  /* DPL=0, Present, Type=0xE */
#define IDT_INTERRUPT_GATE_USER    0xEE  /* DPL=3, Present, Type=0xE */
#define IDT_TRAP_GATE_KERNEL       0x8F  /* DPL=0, Present, Type=0xF */
```

## Critical Sections

Interrupts can be disabled/enabled:
```c
cli();  /* Disable interrupts */
/* Critical section */
sti();  /* Enable interrupts */
```

Or using the architecture interface:
```c
arch_disable_interrupts();
/* Critical section */
arch_enable_interrupts();
```

## Nested Interrupts

Currently **NOT supported**. Interrupts are disabled during handler execution.

## Future Work

- [ ] APIC support (replace PIC)
- [ ] MSI/MSI-X support
- [ ] Nested interrupt support
- [ ] Per-CPU interrupt handling (SMP)
- [ ] Interrupt affinity
- [ ] Deferred interrupt handling (softirqs)
