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
    
    ; CRITICAL: Save syscall number BEFORE overwriting rax!
    mov rdi, rax    ; Save syscall number FIRST
    
    ; Switch to kernel segments (this overwrites ax/rax!)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    
    ; Setup syscall arguments correctly
    ; Shell passes: rax=syscall_num, rbx=arg1, rcx=arg2, rdx=arg3
    ; C function expects: (rdi=syscall_num, rsi=arg1, rdx=arg2, rcx=arg3, r8=arg4, r9=arg5)
    
    ; rdi already has syscall number (saved above)
    mov rsi, rbx    ; arg1
    mov r10, rcx    ; Save arg2 temporarily
    mov r11, rdx    ; Save arg3 temporarily
    mov rdx, r10    ; arg2 (from rcx)
    mov rcx, r11    ; arg3 (from rdx)
    mov r8, 0       ; arg4 (unused)
    mov r9, 0       ; arg5 (unused)
    
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