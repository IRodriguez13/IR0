# IR0 Interrupt System

IR0 implements interrupt handling with IDT management, PIC configuration, and ISR
routing. The x86-64 backend now lives under `arch/x86-64/`; portable code enters
through `ir0/irq.h`.

## Components

### x86-64 Backend (`arch/x86-64/`)
- `sources/interrupt/idt.c` / `include/interrupt/idt.h` - Interrupt Descriptor Table
- `sources/interrupt/pic.c` / `include/interrupt/pic.h` - Programmable Interrupt Controller
- `sources/interrupt/isr_handlers.c` - Interrupt Service Routine routing
- `sources/interrupt/keyboard.c` / `include/interrupt/keyboard.h` - Keyboard IRQ handler
- `include/interrupt/io.h` - I/O port operations for interrupts
- `asm/interrupt/isr_stubs_64.asm` - Assembly interrupt stubs
- `asm/interrupt/early_idt_stubs.asm` - Early-boot exception stubs

## Features

### Interrupt Descriptor Table (IDT)
- ✅ Complete IDT setup with 64 interrupt vectors
- ✅ Exception handlers (0-31)
- ✅ Hardware interrupt handlers (32-47)
- ✅ Software interrupt handlers (48-255)
- ✅ Proper privilege level configuration
- ✅ Gate type configuration (interrupt/trap gates)

### Programmable Interrupt Controller (PIC)
- ✅ PIC initialization and remapping
- ✅ IRQ masking and unmasking
- ✅ End-of-interrupt (EOI) handling
- ✅ Cascade configuration for dual PIC setup
- ✅ IRQ priority management

### Hardware Interrupts
- ✅ Timer interrupt (IRQ 0) - System timer
- ✅ Keyboard interrupt (IRQ 1) - PS/2 keyboard
- ✅ Sound Blaster interrupt (IRQ 5) - Audio
- ✅ Mouse interrupt (IRQ 12) - PS/2 mouse
- ✅ Primary IDE interrupt (IRQ 14) - Storage
- ✅ Secondary IDE interrupt (IRQ 15) - Storage

### Exception Handling
- ✅ Division by zero (INT 0)
- ✅ Debug exception (INT 1)
- ✅ Non-maskable interrupt (INT 2)
- ✅ Breakpoint (INT 3)
- ✅ Overflow (INT 4)
- ✅ Bound range exceeded (INT 5)
- ✅ Invalid opcode (INT 6)
- ✅ Device not available (INT 7)
- ✅ Double fault (INT 8)
- ✅ Invalid TSS (INT 10)
- ✅ Segment not present (INT 11)
- ✅ Stack fault (INT 12)
- ✅ General protection fault (INT 13)
- ✅ Page fault (INT 14)
- ✅ x87 FPU error (INT 16)

### Software Interrupts
- ✅ System call interface (INT 0x80)
- ✅ Custom software interrupts
- ✅ Inter-processor interrupts (framework)

## Architecture

### IDT Structure
```c
// IDT entry structure
struct idt_entry {
    uint16_t offset_low;    // Lower 16 bits of handler address
    uint16_t selector;      // Kernel segment selector
    uint8_t ist;           // Interrupt Stack Table offset
    uint8_t type_attr;     // Type and attributes
    uint16_t offset_mid;   // Middle 16 bits of handler address
    uint32_t offset_high;  // Upper 32 bits of handler address
    uint32_t zero;         // Reserved
} __attribute__((packed));

// IDT descriptor
struct idt_descriptor {
    uint16_t limit;        // Size of IDT - 1
    uint64_t base;         // Base address of IDT
} __attribute__((packed));
```

### Interrupt Handler Flow
```
1. Hardware/Software interrupt occurs
2. CPU saves current state on stack
3. CPU looks up handler in IDT
4. Jump to assembly stub (isr_stubs_64.asm)
5. Assembly stub saves registers
6. Call C interrupt handler
7. C handler processes interrupt
8. Send EOI to PIC (if hardware interrupt)
9. Assembly stub restores registers
10. Return from interrupt (IRET)
```

### PIC Configuration
```c
// PIC ports
#define PIC1_COMMAND    0x20
#define PIC1_DATA       0x21
#define PIC2_COMMAND    0xA0
#define PIC2_DATA       0xA1

// PIC initialization
void pic_remap(int offset1, int offset2);
void pic_mask_irq(uint8_t irq);
void pic_unmask_irq(uint8_t irq);
void pic_send_eoi(uint8_t irq);
```

## Interrupt Handlers

### Timer Interrupt (IRQ 0)
- System tick for scheduler
- Time keeping
- Periodic tasks
- Scheduler preemption

### Keyboard Interrupt (IRQ 1)
- PS/2 keyboard input
- Scancode processing
- Key event generation
- Buffer management

### Mouse Interrupt (IRQ 12)
- PS/2 mouse input
- Movement tracking
- Button state changes
- Scroll wheel events

### Storage Interrupts (IRQ 14/15)
- ATA/IDE completion
- Error handling
- DMA transfer completion
- Device status updates

### Audio Interrupt (IRQ 5)
- Sound Blaster completion
- Audio buffer management
- Playback control
- DMA transfer status

## System Call Interface

### INT 0x80 Handler
```c
// System call entry point
void syscall_handler(void) {
    // Get system call number from RAX
    uint64_t syscall_num = get_register(RAX);
    
    // Get arguments from registers
    uint64_t arg1 = get_register(RBX);
    uint64_t arg2 = get_register(RCX);
    uint64_t arg3 = get_register(RDX);
    
    // Call system call dispatcher
    uint64_t result = handle_syscall(syscall_num, arg1, arg2, arg3);
    
    // Return result in RAX
    set_register(RAX, result);
}
```

## Current Status

### ✅ Fully Functional
- IDT setup and management
- PIC configuration and control
- Exception handling
- Hardware interrupt handling
- System call interface
- Assembly interrupt stubs

### ✅ Working Interrupts
- Timer (IRQ 0) - System scheduling
- Keyboard (IRQ 1) - User input
- Mouse (IRQ 12) - Mouse input
- Storage (IRQ 14/15) - Disk I/O
- Audio (IRQ 5) - Sound output
- System calls (INT 0x80) - Kernel services

### ⚠️ Limitations
- Single CPU support only
- Basic exception handling
- No advanced interrupt features (MSI, etc.)
- Limited debugging support

### 🔄 Potential Improvements
- SMP interrupt handling
- Advanced PIC features
- MSI/MSI-X support
- Better exception debugging
- Interrupt statistics and monitoring

## Build Integration

The interrupt system is built as part of the main kernel:
- Assembly stubs compiled with NASM
- C handlers compiled with GCC
- Architecture-specific optimizations
- Debug symbol generation

## Testing

Interrupt functionality can be tested through:
- Keyboard input (IRQ 1)
- Mouse movement (IRQ 12)
- Timer-based operations (IRQ 0)
- System call usage (INT 0x80)
- Exception triggering (for debugging)
- Storage operations (IRQ 14/15)