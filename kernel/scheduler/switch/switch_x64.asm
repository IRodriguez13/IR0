global switch_context_x64

section .text

switch_context_x64:
    test rdi, rdi
    jz .done
    test rsi, rsi
    jz .done
    
    mov [rdi + 0x00], rax
    mov [rdi + 0x08], rbx
    mov [rdi + 0x10], rcx
    mov [rdi + 0x18], rdx
    mov [rdi + 0x20], rsi
    mov rax, rdi
    mov [rdi + 0x28], rax
    mov [rdi + 0x30], r8
    mov [rdi + 0x38], r9
    mov [rdi + 0x40], r10
    mov [rdi + 0x48], r11
    mov [rdi + 0x50], r12
    mov [rdi + 0x58], r13
    mov [rdi + 0x60], r14
    mov [rdi + 0x68], r15
    mov [rdi + 0x70], rsp
    mov [rdi + 0x78], rbp
    mov rax, [rsp]
    mov [rdi + 0x80], rax
    pushfq
    pop rax
    mov [rdi + 0x88], rax
    mov ax, cs
    mov [rdi + 0x90], ax
    mov ax, ds
    mov [rdi + 0x92], ax
    mov ax, es
    mov [rdi + 0x94], ax
    mov ax, fs
    mov [rdi + 0x96], ax
    mov ax, gs
    mov [rdi + 0x98], ax
    mov ax, ss
    mov [rdi + 0x9A], ax
    mov rax, cr3
    mov [rdi + 0xB0], rax
    
    ; Guardar rdi original (contexto actual) en rcx
    mov rcx, rdi
    ; Mover nuevo contexto a rdi
    mov rdi, rsi
    
    mov rax, [rdi + 0xB0]
    mov cr3, rax
    mov ax, [rdi + 0x9A]
    mov ss, ax
    mov ax, [rdi + 0x92]
    mov ds, ax
    mov ax, [rdi + 0x94]
    mov es, ax
    mov ax, [rdi + 0x96]
    mov fs, ax
    mov ax, [rdi + 0x98]
    mov gs, ax
    mov rax, [rdi + 0x00]
    mov rbx, [rdi + 0x08]
    mov rcx, [rdi + 0x10]
    mov rdx, [rdi + 0x18]
    mov rsi, [rdi + 0x20]
    mov r8, [rdi + 0x30]
    mov r9, [rdi + 0x38]
    mov r10, [rdi + 0x40]
    mov r11, [rdi + 0x48]
    mov r12, [rdi + 0x50]
    mov r13, [rdi + 0x58]
    mov r14, [rdi + 0x60]
    mov r15, [rdi + 0x68]
    mov rsp, [rdi + 0x70]
    mov rbp, [rdi + 0x78]
    push qword [rdi + 0x88]
    popfq
    ; Restaurar rdi desde el contexto guardado (rcx)
    push qword [rdi + 0x80]
    mov rdi, [rcx + 0x28]
    iretq

.done:
    iretq

section .note.GNU-stack noalloc noexec nowrite progbits
