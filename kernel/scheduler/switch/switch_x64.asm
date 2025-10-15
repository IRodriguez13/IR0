global switch_context_x64

section .text

switch_context_x64:
    ; Guardar todos los registros
    push rbp
    mov rbp, rsp
    push r15
    push r14
    push r13
    push r12
    push rbx
    
    ; Log: Inicio del cambio de contexto
    mov r15, rdi  ; Contexto actual
    mov r14, rsi  ; Nuevo contexto
    
    ; Verificar punteros nulos
    test r15, r15
    jz .done
    test r14, r14
    jz .done
     
    ; 1. Guardar contexto actual
    mov [r15 + 0x00], rax
    mov [r15 + 0x08], rbx
    mov [r15 + 0x10], rcx
    mov [r15 + 0x18], rdx
    mov [r15 + 0x20], rsi
    mov rax, r15
    mov [r15 + 0x28], rax  ; Guardar puntero a la estructura
    
    ; Guardar registros restantes
    mov [r15 + 0x30], r8
    mov [r15 + 0x38], r9
    mov [r15 + 0x40], r10
    mov [r15 + 0x48], r11
    mov [r15 + 0x50], r12
    mov [r15 + 0x58], r13
    mov [r15 + 0x60], r14
    
    ; Guardar rsp y rbp
    mov [r15 + 0x70], rsp
    mov [r15 + 0x78], rbp
    
    ; Guardar RIP (dirección de retorno)
    mov rax, [rsp + 8*7]  ; Saltar los registros guardados
    mov [r15 + 0x80], rax
    
    ; Guardar RFLAGS
    pushfq
    pop qword [r15 + 0x88]
    
    ; Guardar registros de segmento
    mov ax, cs
    mov [r15 + 0x90], ax
    mov ax, ds
    mov [r15 + 0x92], ax
    mov ax, es
    mov [r15 + 0x94], ax
    mov ax, fs
    mov [r15 + 0x96], ax
    mov ax, gs
    mov [r15 + 0x98], ax
    mov ax, ss
    mov [r15 + 0x9A], ax
    
    ; Guardar CR3
    mov rax, cr3
    mov [r15 + 0xB0], rax
    
    
    ; Cargar CR3 (cambio de espacio de direcciones)
    mov rax, [r14 + 0xB0]
    mov cr3, rax
    
    ; Cargar registros de segmento
    mov ax, [r14 + 0x9A]  ; SS
    mov ss, ax
    mov ax, [r14 + 0x92]  ; DS
    mov ds, ax
    mov ax, [r14 + 0x94]  ; ES
    mov es, ax
    mov ax, [r14 + 0x96]  ; FS
    mov fs, ax
    mov ax, [r14 + 0x98]  ; GS
    mov gs, ax
    
    ; Cargar registros de propósito general
    mov rax, [r14 + 0x00]
    mov rbx, [r14 + 0x08]
    mov rcx, [r14 + 0x10]
    mov rdx, [r14 + 0x18]
    mov rsi, [r14 + 0x20]
    
    ; Cargar registros adicionales
    mov r8,  [r14 + 0x30]
    mov r9,  [r14 + 0x38]
    mov r10, [r14 + 0x40]
    mov r11, [r14 + 0x48]
    mov r12, [r14 + 0x50]
    mov r13, [r14 + 0x58]
    
    ; Cargar RSP y RBP
    mov rsp, [r14 + 0x70]
    mov rbp, [r14 + 0x78]
    
    ; Cargar RFLAGS
    push qword [r14 + 0x88]
    popfq
    
    ; Preparar pila para iretq
    push qword [r14 + 0x9A]  ; SS
    push rsp                 ; RSP
    push qword [r14 + 0x88]  ; RFLAGS
    push qword [r14 + 0x90]  ; CS
    push qword [r14 + 0x80]  ; RIP
    
    ; Cargar RDI (último registro a restaurar)
    mov rdi, [r14 + 0x28]
      
    ; Retornar al nuevo contexto
    iretq

.done:
    ; Limpiar pila y retornar
    pop rbx
    pop r12
    pop r13
    pop r14
    pop r15
    pop rbp
    iretq

section .rodata
.current_ctx_msg:      db "SERIAL: [CTX] Saving current context for PID: ", 0
.saved_ctx_msg:        db "SERIAL: [CTX] Context saved for PID: ", 0
.loading_ctx_msg:      db "SERIAL: [CTX] Loading context for PID: ", 0
.context_switch_done_msg: db "SERIAL: [CTX] Context switch completed\n", 0

section .note.GNU-stack noalloc noexec nowrite progbits
