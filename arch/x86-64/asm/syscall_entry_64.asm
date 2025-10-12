; System call entry point for int 0x80
[BITS 64]

global syscall_entry_asm
extern syscall_dispatch

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
    
    ; Save syscall arguments BEFORE overwriting anything
    ; Shell passes: rax=syscall_num, rbx=arg1, rcx=arg2, rdx=arg3, rsi=arg4, rdi=arg5
    mov r10, rax    ; Save syscall number
    mov r11, rbx    ; Save arg1
    mov r12, rcx    ; Save arg2  
    mov r13, rdx    ; Save arg3
    mov r14, rsi    ; Save arg4
    mov r15, rdi    ; Save arg5
    
    ; Switch to kernel segments (this overwrites ax/rax!)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    
    ; Setup syscall arguments for C function
    ; C function expects: (rdi=syscall_num, rsi=arg1, rdx=arg2, rcx=arg3, r8=arg4, r9=arg5)
    mov rdi, r10    ; syscall number
    mov rsi, r11    ; arg1
    mov rdx, r12    ; arg2
    mov rcx, r13    ; arg3
    mov r8, r14     ; arg4
    mov r9, r15     ; arg5
    
    ; Call C dispatcher
    call syscall_dispatch
    
    ; Return value in rax (already set by syscall_dispatch)
    
    ; Restore registers
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
    add rsp, 8      ; Skip saved rax, use return value instead
    
    ; No need to send EOI for syscalls (they are software interrupts)
    
    ; Return to user mode
    iretq
    

section .note.GNU-stack noalloc noexec nowrite progbits