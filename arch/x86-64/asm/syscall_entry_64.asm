; System call entry point
[BITS 64]

global syscall_entry_asm
extern syscall_handler_c

section .text

syscall_entry_asm:
    ; Save all registers
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
    
    ; Switch to kernel segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Call C handler
    call syscall_handler_c
    
    ; Restore registers if returns
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
    
    ; Return to user mode
    iretq