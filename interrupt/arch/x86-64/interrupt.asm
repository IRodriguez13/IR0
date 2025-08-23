; interrupt/arch/x86-64/interrupt.asm - IMPLEMENTACIÓN MÍNIMA Y FUNCIONAL
BITS 64

; Variables globales
extern isr_handler

; Macro para crear stubs sin error code
%macro ISR_NOERRCODE 1
global isr_stub_%1
isr_stub_%1:
    cli                     ; Deshabilitar interrupciones
    push qword 0           ; Error code dummy
    push qword %1          ; Número de interrupción
    jmp isr_common_stub
%endmacro

; Macro para crear stubs con error code
%macro ISR_ERRCODE 1
global isr_stub_%1
isr_stub_%1:
    cli                     ; Deshabilitar interrupciones
    push qword %1          ; Número de interrupción
    jmp isr_common_stub
%endmacro

; Crear todos los stubs
ISR_NOERRCODE 0   ; Divide by zero
ISR_NOERRCODE 1   ; Debug
ISR_NOERRCODE 2   ; Non-maskable interrupt
ISR_NOERRCODE 3   ; Breakpoint
ISR_NOERRCODE 4   ; Overflow
ISR_NOERRCODE 5   ; Bound range exceeded
ISR_NOERRCODE 6   ; Invalid opcode
ISR_NOERRCODE 7   ; Device not available
ISR_ERRCODE 8     ; Double fault
ISR_NOERRCODE 9   ; Coprocessor segment overrun
ISR_ERRCODE 10    ; Invalid TSS
ISR_ERRCODE 11    ; Segment not present
ISR_ERRCODE 12    ; Stack segment fault
ISR_ERRCODE 13    ; General protection fault
ISR_ERRCODE 14    ; Page fault
ISR_NOERRCODE 15  ; Reserved
ISR_NOERRCODE 16  ; x87 FPU error
ISR_ERRCODE 17    ; Alignment check
ISR_NOERRCODE 18  ; Machine check
ISR_NOERRCODE 19  ; SIMD FPU error
ISR_NOERRCODE 20  ; Virtualization error
ISR_NOERRCODE 21  ; Reserved
ISR_NOERRCODE 22  ; Reserved
ISR_NOERRCODE 23  ; Reserved
ISR_NOERRCODE 24  ; Reserved
ISR_NOERRCODE 25  ; Reserved
ISR_NOERRCODE 26  ; Reserved
ISR_NOERRCODE 27  ; Reserved
ISR_NOERRCODE 28  ; Reserved
ISR_NOERRCODE 29  ; Reserved
ISR_NOERRCODE 30  ; Security exception
ISR_NOERRCODE 31  ; Reserved

; IRQs
ISR_NOERRCODE 32  ; Timer
ISR_NOERRCODE 33  ; Keyboard

; Stub común para todas las interrupciones
isr_common_stub:
    ; Guardar todos los registros
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    ; Llamar al handler C
    mov rdi, [rsp + 120]   ; Obtener número de interrupción
    call isr_handler
    
    ; Restaurar registros
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    
    ; Limpiar error code y número de interrupción
    add rsp, 16
    
    ; Restaurar interrupciones y retornar
    sti
    iretq

; Función para cargar IDT
global idt_load
idt_load:
    lidt [rdi]    ; RDI contiene el puntero al IDT
    ret