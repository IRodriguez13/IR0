; Básicamente, acá tengo funciones que se encargan de orquestar llamados a los diferentes handlers de interrupciones.
; Lo hago en asm porque la funcion flush requiere de lidt que me permite cargar el idt que hice en el alto nivel. 
; Aparte, iret no existe en alto nivel y retornar con los registros puhseados al interrumpir corrompe todo

[bits 64]

global isr_default
global isr_page_fault
global idt_flush
global timer_stub

extern idt_flush
extern page_fault_handler_x64
extern default_interrupt_handler
extern time_handler

idt_flush:
    mov  rax, [rsp+8] ; parámetro pasado (el puntero a idt_ptr, que también es un puntero)
    lidt [rax]        ; cargo la idt usando la memo adrss de su puntero
    ret               ; vuelvo a alto nivel

isr_template: ; Es para testear el tema del byte que quiero mostrar por consola
    ; Guardar registros en 64-bit
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
    push qword 14                   ; la idea es loggear este PF.
    call default_interrupt_handler
    add  rsp, 8
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
    iretq

isr_default:
    ; Guardar registros en 64-bit
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
    call  default_interrupt_handler ; Llama a la funcion en C para manejar la interrupcion normal
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
    iretq                           ; Retorna de la interrupción (especial, no ret normal)

isr_page_fault:
    ; NO hacer add rsp, 8 aquí - el error code lo necesita el handler
    ; Guardar registros en 64-bit
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
    
    ; Obtener error code y fault address
    mov rdi, [rsp + 8*16]  ; Error code está en el stack
    mov rsi, cr2           ; Fault address está en CR2
    
    call page_fault_handler_x64
    ; Limpiar error code DESPUÉS del handler
    add  rsp, 8
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
    iretq

timer_stub: ; lo tengo que hacer porque la cpu no sabe hacer iret en C
    ; Guardar registros en 64-bit
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
    call time_handler
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
    iretq


; Al entrar a la interrupción, CPU hace automáticamente:
;pushf        ; Guarda EFLAGS (incluyendo IF = interrupt flag)
;cli          ; Deshabilita interrupciones (IF = 0)
;push cs      ; Guarda segmento
;push eip     ; Guarda posición actual
;CON error code: 8, 10, 11, 12, 13, 14, 17, 21
;SIN error code: Todas las demás

;el iret me popea todos esos datos desde el momento en que finaliza la interrupcion
;iret
; CPU hace internamente:
; pop eip       ; eip = [esp], esp += 4
; pop cs        ; cs = [esp], esp += 4  
; pop eflags    ; eflags = [esp], esp += 4
;
;
;al momento de hacer iret con errores
;iret
; CPU hace internamente:
; pop eip       ; eip = [esp], esp += 4
; pop cs        ; cs = [esp], esp += 4  
; pop eflags    ; eflags = [esp], esp += 4 
;
; Stack después de una interrupción con error code:
;
;ESP+12 -> [eflags ]  (4 bytes)
;ESP+8  -> [cs     ]  (4 bytes)  
;ESP+4  -> [eip    ]  (4 bytes) <- ESP debería apuntar acá
;ESP+0  -> [error  ]  (4 bytes) <- ESP apunta aquí
;
;La idea es correrlo 4 bytes "arriba" para que el puntero de pila apunte al instruction pointer como debe ser.