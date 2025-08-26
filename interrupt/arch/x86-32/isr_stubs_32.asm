; Stubs de interrupción completos para x86-32
; Implementación desde cero - limpia y funcional

extern isr_handler32

; Macro para stubs sin código de error
%macro ISR_NOERRCODE 1
global isr%1_32
isr%1_32:
    cli
    push byte 0
    push byte %1
    jmp isr_common_stub_32
%endmacro

; Macro para stubs con código de error
%macro ISR_ERRCODE 1
global isr%1_32
isr%1_32:
    cli
    push byte %1
    jmp isr_common_stub_32
%endmacro

; Generar stubs para excepciones (0-31)
ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE 8
ISR_NOERRCODE 9
ISR_ERRCODE 10
ISR_ERRCODE 11
ISR_ERRCODE 12
ISR_ERRCODE 13
ISR_ERRCODE 14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE 17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_NOERRCODE 30
ISR_NOERRCODE 31

; Generar stubs para IRQs (32-47)
ISR_NOERRCODE 32
ISR_NOERRCODE 33
ISR_NOERRCODE 34
ISR_NOERRCODE 35
ISR_NOERRCODE 36
ISR_NOERRCODE 37
ISR_NOERRCODE 38
ISR_NOERRCODE 39
ISR_NOERRCODE 40
ISR_NOERRCODE 41
ISR_NOERRCODE 42
ISR_NOERRCODE 43
ISR_NOERRCODE 44
ISR_NOERRCODE 45
ISR_NOERRCODE 46
ISR_NOERRCODE 47

; Handler común completo
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
    ; El número de interrupción está en [esp + 28]
    mov eax, [esp + 28]
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

    ; Limpiar stack (código de error y número de interrupción)
    add esp, 8

    ; Habilitar interrupciones y retornar
    sti
    iret
