; Stubs de interrupción para x86-64
; Cada stub guarda el número de interrupción y llama al handler común

extern isr_handler64

; Macro para stubs sin código de error
%macro ISR_NOERRCODE 1
global isr%1_64
isr%1_64:
    cli                     ; Deshabilitar interrupciones
    push qword 0            ; Código de error dummy
    push qword %1           ; Número de interrupción
    jmp isr_common_stub_64  ; Ir al handler común
%endmacro

; Macro para stubs con código de error
%macro ISR_ERRCODE 1
global isr%1_64
isr%1_64:
    cli                     ; Deshabilitar interrupciones
    push qword %1           ; Número de interrupción (el código de error ya está en el stack)
    jmp isr_common_stub_64  ; Ir al handler común
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

; Stub para syscall (interrupción 0x80)
global isr128_64
extern syscall_entry_asm
isr128_64:
    jmp syscall_entry_asm   

; Handler común para todas las interrupciones
isr_common_stub_64:
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
    mov rdi, qword [rsp + 120]  ; Obtener número de interrupción del stack
    call isr_handler64

    ; Restaurar todos los registros
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

    ; Limpiar código de error y número de interrupción
    add rsp, 16

    ; Habilitar interrupciones y retornar
    sti
    iretq
; ===============================================================================
; GNU STACK SECTION - Prevents executable stack warning
; ===============================================================================
section .note.GNU-stack noalloc noexec nowrite progbits