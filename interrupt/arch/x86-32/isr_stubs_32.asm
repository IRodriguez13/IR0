; Stubs de interrupción para x86-32
; Cada stub guarda el número de interrupción y llama al handler común

extern isr_handler32

; Macro para stubs sin código de error
%macro ISR_NOERRCODE 1
global isr%1_32
isr%1_32:
    cli                     ; Deshabilitar interrupciones
    push dword 0            ; Código de error dummy
    push dword %1           ; Número de interrupción
    jmp isr_common_stub_32  ; Ir al handler común
%endmacro

; Macro para stubs con código de error
%macro ISR_ERRCODE 1
global isr%1_32
isr%1_32:
    cli                     ; Deshabilitar interrupciones
    push dword %1           ; Número de interrupción (el código de error ya está en el stack)
    jmp isr_common_stub_32  ; Ir al handler común
%endmacro

; Generar stubs para excepciones (0-31)
ISR_NOERRCODE 0   ; Divide Error
ISR_NOERRCODE 1   ; Debug Exception
ISR_NOERRCODE 2   ; Non-maskable Interrupt
ISR_NOERRCODE 3   ; Breakpoint
ISR_NOERRCODE 4   ; Overflow
ISR_NOERRCODE 5   ; Bound Range Exceeded
ISR_NOERRCODE 6   ; Invalid Opcode
ISR_NOERRCODE 7   ; Device Not Available
ISR_ERRCODE 8     ; Double Fault
ISR_NOERRCODE 9   ; Coprocessor Segment Overrun
ISR_ERRCODE 10    ; Invalid TSS
ISR_ERRCODE 11    ; Segment Not Present
ISR_ERRCODE 12    ; Stack Segment Fault
ISR_ERRCODE 13    ; General Protection Fault
ISR_ERRCODE 14    ; Page Fault
ISR_NOERRCODE 15  ; Reserved
ISR_NOERRCODE 16  ; x87 FPU Error
ISR_ERRCODE 17    ; Alignment Check
ISR_NOERRCODE 18  ; Machine Check
ISR_NOERRCODE 19  ; SIMD FPU Exception
ISR_NOERRCODE 20  ; Virtualization Exception
ISR_NOERRCODE 21  ; Reserved
ISR_NOERRCODE 22  ; Reserved
ISR_NOERRCODE 23  ; Reserved
ISR_NOERRCODE 24  ; Reserved
ISR_NOERRCODE 25  ; Reserved
ISR_NOERRCODE 26  ; Reserved
ISR_NOERRCODE 27  ; Reserved
ISR_NOERRCODE 28  ; Reserved
ISR_NOERRCODE 29  ; Reserved
ISR_NOERRCODE 30  ; Reserved
ISR_NOERRCODE 31  ; Reserved

; Generar stubs para IRQs (32-47)
ISR_NOERRCODE 32  ; Timer
ISR_NOERRCODE 33  ; Keyboard
ISR_NOERRCODE 34  ; Cascade
ISR_NOERRCODE 35  ; COM2
ISR_NOERRCODE 36  ; COM1
ISR_NOERRCODE 37  ; LPT2
ISR_NOERRCODE 38  ; Floppy
ISR_NOERRCODE 39  ; LPT1
ISR_NOERRCODE 40  ; CMOS
ISR_NOERRCODE 41  ; Free
ISR_NOERRCODE 42  ; Free
ISR_NOERRCODE 43  ; Free
ISR_NOERRCODE 44  ; PS2
ISR_NOERRCODE 45  ; FPU
ISR_NOERRCODE 46  ; ATA1
ISR_NOERRCODE 47  ; ATA2

; Handler común para todas las interrupciones
isr_common_stub_32:
    ; Guardar todos los registros
    push eax
    push ecx
    push edx
    push ebx
    push ebp
    push esi
    push edi

    ; Llamar al handler C
    mov eax, [esp + 28]  ; Obtener número de interrupción del stack
    push eax
    call isr_handler32
    add esp, 4

    ; Restaurar todos los registros
    pop edi
    pop esi
    pop ebp
    pop ebx
    pop edx
    pop ecx
    pop eax

    ; Limpiar código de error y número de interrupción
    add esp, 8

    ; Habilitar interrupciones y retornar
    sti
    iret
