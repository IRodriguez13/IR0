# Interrupciones y excepciones de IR0

| Campo | Valor |
|-------|-------|
| Versión | 0.1 |
| Fase IR0 | T0 |
| Estado | stable |
| Depende de | boot, drivers, syscalls |
| Página man | IR0-interrupts (sección 7) |
| Fuentes principales | `interrupt/arch/{idt,pic,isr_handlers}.c`, `interrupt/arch/x86-64/isr_stubs_64.asm`, `arch/x86-64/asm/syscall_*.asm`, `arch/x86-64/sources/fault.c` |

## 1. Visión general

El manejo de interrupciones x86-64 usa una IDT de 256 entradas, PIC 8259 dual
(IRQ 0–15 → vectores 32–47), stubs en ensamblador y handlers C en
`isr_handler64`. Las syscalls entran vía **int 0x80** (ABI debug_bins) e
instrucción **`syscall`** (ABI musl/Linux). Las excepciones de CPU se mapean a
señales para tareas de usuario o panic para fallos en kernel.

## 2. Arquitectura interna

| Componente | Ruta |
|------------|------|
| IDT | `interrupt/arch/idt.c` — 256 gates, `#DF` usa IST1 |
| PIC | `interrupt/arch/pic.c` — remap bases 0x20/0x28, mask/unmask, EOI |
| Stubs | `isr_stubs_64.asm` → `isr_common_stub_64` → `isr_handler64` |
| Syscall int | `syscall_entry_64.asm` — IDT 0x80 / vector 128 salto a entry |
| Syscall insn | `syscall_insn_entry_64.asm` — LSTAR, kstack 8 KiB |
| Page fault | `fault.c` — demand-fill o SIGSEGV |

## 3. Flujo de datos

**IRQ hardware:**

```text
  línea IRQ ──► vector IDT 32+irq
       ──► isr_stub (cli, guardar regs)
       ──► isr_handler64(vector, frame)
       ──► handler dispositivo (timer/teclado/ratón/red)
       ──► pic_send_eoi64
       ──► iretq
```

**Syscall int 0x80:**

```text
  int $0x80 ──► syscall_entry_asm
       ──► mapear regs debug_bins → ABI C Linux
       ──► syscall_dispatch()
       ──► iretq (segmentos usuario 0x23)
```

**IRQs manejadas en switch:**

```text
  IRQ0  (32) PIT + clock_tick_handler
  IRQ1  (33) keyboard_handler64
  IRQ12 (44) input_mouse_handle_interrupt (si ratón habilitado)
  NIC   net_stack_handle_irq (si CONFIG_ENABLE_NETWORKING)
  otros: solo EOI (sin handler de driver aún)
```

## 4. Responsabilidades

- IDT: instalar gates antes de `sti`.
- PIC: todas las IRQ enmascaradas al init; `arch_boot_irq_unmask` habilita timer, teclado, cascade, ratón, NIC.
- ISR: excepciones usuario → señales; excepciones kernel → panic (salvo rutas audit #PF/#GP).
- Entrada syscall: capturar frame usuario para fork/reanudación de señal en x86-64.

## 5. Límites del subsistema

- El código portable no debe `#include <interrupt/arch/...>` — usar fachadas y arch_port.
- Handler vector 128 en C es no-op; el int 0x80 real salta directo al asm syscall.
- Sin APIC/IOAPIC como path primario — solo PIC legacy.

## 6. Relación con otros subsistemas

| Vecino | Interacción |
|--------|-------------|
| Timer | IRQ0 → PIT → quantum del planificador (preempt `#if 0`) |
| TTY/Input | IRQ1 teclado, IRQ12 ratón |
| Net | IRQ NIC → rtl8139 |
| Scheduler | contexto IRQ no debe HLT |
| Signals | ISR envía señales por excepciones CPU usuario |

## 7. Mapas visuales

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

## 8. Invariantes importantes

1. Gate invocable desde usuario: solo syscall (DPL=3 en vector 0x80).
2. Excepciones HW con código de error: vectores 8, 10–14, 17.
3. `#PF` usuario: demand-fill heap/mmap o SIGSEGV + exit.
4. `#PF` kernel: panic.
5. `CONFIG_DEBUG_ISRABI` audita contrato de frame iretq.

## 9. Consejos de depuración

- Tags: `[ISR]`, `[ISRABI]`, `[PF_AUDIT]`, `[GPF_AUDIT]`, `[BOOT] Interrupts enabled`.
- `/proc/interrupts` — snapshot de contadores.
- La mayoría de líneas IRQ (COM, IDE) tienen stubs sin handlers.

Legacy: `Documentation/INTERRUPTS.md`, `interrupt/README.md`.

## 10. Roadmap futuro

- APIC/LAPIC como controlador primario — solo código parcial de timer LAPIC.
- Handlers para IRQs ATA/COM — no cableados en switch ISR.
- Excepciones kernel recuperables — hoy mayormente panic.
- Unificar path redundante vector 128 con entrada int 0x80.
