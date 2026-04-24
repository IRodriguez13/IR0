# Ruta de Interrupciones y Excepciones en IR0

IR0 usa flujo x86-64 con IDT + PIC, gateway de syscalls e integracion de
excepciones con senales hacia procesos.

## Componentes Base

- Setup/carga de IDT en `interrupt/arch/idt.c`.
- Setup de PIC y manejo IRQ mask/unmask en `interrupt/arch/pic.c`.
- Stubs ISR en assembly de arquitectura.
- Routing y handlers en `interrupt/arch/isr_handlers.c`.

## Comportamiento Runtime

- Las excepciones de CPU se enrutan por ISR y se mapean a senales de proceso.
- Las IRQ de hardware envian EOI despues del dispatch del handler.
- Los syscalls entran por tabla de dispatch en el subsistema de syscalls.

## Caracteristicas Relevantes

- El modelo PIC legacy sigue siendo el default.
- El manejo de excepciones busca continuidad del sistema cuando es posible.
- La entrega de senales esta integrada con scheduler/procesos.

## Puntos Fuertes

- Ruta de bring-up estable y facil de seguir.
- Buena visibilidad por logs seriales y `/proc/interrupts`.
- El mapeo a senales reduce colapsos globales por fallas de user space.

## Puntos Debiles

- El escalado de interrupciones tipo APIC/SMP no es el path por defecto.
- Threading de interrupciones y priorizacion fina aun son limitados.
