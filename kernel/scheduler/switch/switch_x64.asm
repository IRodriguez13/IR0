; switch_x64.asm - CRÍTICO para scheduler en 64-bit
global switch_task_64

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
    
    ; Guardar RSP en old_task->rsp
    mov [rdi + 8], rsp    ; old_task->rsp (64-bit)
    
    ; Cargar nuevo contexto
    mov rsp, [rsi + 8]    ; new_task->rsp
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