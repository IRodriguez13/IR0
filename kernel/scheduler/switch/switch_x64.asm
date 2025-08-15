; switch_x64.asm - CRÍTICO para scheduler en 64-bit
global switch_task_64
global switch_task

switch_task_64:
    cli
    
    ; Verificar que los parámetros no sean NULL
    test rdi, rdi
    jz .skip_save
    test rsi, rsi
    jz .skip_save
    
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
    
    ; Guardar RSP en old_task->esp
    mov rax, rsp
    mov [rdi + 8], rax
    
    ; Cargar nuevo contexto
    mov rax, [rsi + 8]
    mov rsp, rax
    
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
    
.skip_save:
    sti
    ret

; Alias para compatibilidad con código C
switch_task:
    jmp switch_task_64