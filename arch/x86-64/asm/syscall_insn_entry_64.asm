; Linux x86-64 syscall instruction entry (MSR LSTAR)
[BITS 64]

global syscall_insn_entry_asm
extern syscall_dispatch

section .text

syscall_insn_entry_asm:
    ; rax=nr, rdi/rsi/rdx/r10/r8/r9=args, rcx=user_rip, r11=user_rflags
    push rcx
    push r11
    push r15
    push r14
    push r13
    push r12
    push rbp
    push rbx

    mov r15, rax
    mov r14, rdi
    mov r13, rsi
    mov r12, rdx
    mov r11, r10
    mov r10, r8
    mov r8, r9

    mov ax, 0x10
    mov ds, ax
    mov es, ax

    sub rsp, 8
    mov [rsp], r8

    mov rdi, r15
    mov rsi, r14
    mov rdx, r13
    mov rcx, r12
    mov r8, r11
    mov r9, r10

    call syscall_dispatch
    add rsp, 8

    pop rbx
    pop rbp
    pop r12
    pop r13
    pop r14
    pop r15
    pop r11
    pop rcx

    o64 sysret

section .note.GNU-stack noalloc noexec nowrite progbits
