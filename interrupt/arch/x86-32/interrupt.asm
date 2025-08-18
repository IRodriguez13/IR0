; interrupt/arch/x86-32/interrupt.asm - IMPLEMENTACIÓN MÍNIMA Y FUNCIONAL
BITS 32

; Variables globales
extern isr_handler

; Macro para crear stubs sin error code
%macro ISR_NOERRCODE 1
global isr_stub_%1
isr_stub_%1:
    cli                     ; Deshabilitar interrupciones
    push dword 0           ; Error code dummy
    push dword %1          ; Número de interrupción
    jmp isr_common_stub
%endmacro

; Macro para crear stubs con error code
%macro ISR_ERRCODE 1
global isr_stub_%1
isr_stub_%1:
    cli                     ; Deshabilitar interrupciones
    push dword %1          ; Número de interrupción
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
    push eax
    push ecx
    push edx
    push ebx
    push ebp
    push esi
    push edi
    
    ; Llamar al handler C
    mov eax, [esp + 28]   ; Obtener número de interrupción
    push eax
    call isr_handler
    add esp, 4
    
    ; Restaurar registros
    pop edi
    pop esi
    pop ebp
    pop ebx
    pop edx
    pop ecx
    pop eax
    
    ; Limpiar error code y número de interrupción
    add esp, 8
    
    ; Restaurar interrupciones y retornar
    sti
    iret

; Función para cargar IDT
global idt_load
idt_load:
    mov eax, [esp + 4]    ; Parámetro pasado en stack
    lidt [eax]            ; Cargar IDT
    ret