; switch_x64.asm - CRÍTICO para scheduler en 64-bit
global switch_task_64
global switch_task

switch_task_64:
    cli
    
    ; Guardar contexto actual (64-bit registers)
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    pushfq
    
    ; Guardar RSP en old_task->rsp (64-bit)
    ; Usar registros temporales para evitar problemas de tamaño
    mov rax, rsp
    mov [rdi + 8], rax    ; old_task->rsp (64-bit)
    
    ; Cargar nuevo contexto
    mov rax, [rsi + 8]    ; new_task->rsp
    mov rsp, rax
    mov rax, [rsi + 24]   ; new_task->cr3 (64-bit)
    mov cr3, rax
    
    ; Restaurar registros
    popfq
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    
    sti
    ret

; Alias para compatibilidad con código C
switch_task:
    ; En 64-bit, los parámetros vienen en rdi (current) y rsi (next)
    ; switch_task_64 ya espera los parámetros en estos registros
    jmp switch_task_64