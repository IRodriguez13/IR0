global switch_context_x64

section .text

switch_context_x64:
    ; Save all registers
    push rbp
    mov rbp, rsp
    push r15
    push r14
    push r13
    push r12
    push rbx
    
    ; Log: Context switch start
    mov r15, rdi  ; Current context
    mov r14, rsi  ; New context
    
    ; Check for null pointers
    test r15, r15
    jz .done
    test r14, r14
    jz .done
     
    ; 1. Save current context
    mov [r15 + 0x00], rax
    mov [r15 + 0x08], rbx
    mov [r15 + 0x10], rcx
    mov [r15 + 0x18], rdx
    mov [r15 + 0x20], rsi
    mov rax, r15
    mov [r15 + 0x28], rax  ; Save pointer to structure
    
    ; Save remaining registers
    mov [r15 + 0x30], r8
    mov [r15 + 0x38], r9
    mov [r15 + 0x40], r10
    mov [r15 + 0x48], r11
    mov [r15 + 0x50], r12
    mov [r15 + 0x58], r13
    mov [r15 + 0x60], r14
    
    ; Save rsp and rbp
    mov [r15 + 0x70], rsp
    mov [r15 + 0x78], rbp
    
    ; Save RIP (return address)
    mov rax, [rsp + 8*7]  ; Skip saved registers
    mov [r15 + 0x80], rax
    
    ; Save RFLAGS
    pushfq
    pop qword [r15 + 0x88]
    
    ; Save segment registers
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
    
    ; Save CR3
    mov rax, cr3
    mov [r15 + 0xB0], rax
    
    
    ; Load CR3 (address space change)
    mov rax, [r14 + 0xB0]
    mov cr3, rax
    
    ; Load segment registers
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
    
    ; Load general purpose registers
    mov rax, [r14 + 0x00]
    mov rbx, [r14 + 0x08]
    mov rcx, [r14 + 0x10]
    mov rdx, [r14 + 0x18]
    mov rsi, [r14 + 0x20]
    
    ; Load additional registers
    mov r8,  [r14 + 0x30]
    mov r9,  [r14 + 0x38]
    mov r10, [r14 + 0x40]
    mov r11, [r14 + 0x48]
    mov r12, [r14 + 0x50]
    mov r13, [r14 + 0x58]
    
    ; Load RSP and RBP
    mov rsp, [r14 + 0x70]
    mov rbp, [r14 + 0x78]
    
    ; Load RFLAGS
    push qword [r14 + 0x88]
    popfq
    
    ; Prepare stack for iretq
    push qword [r14 + 0x9A]  ; SS
    push rsp                 ; RSP
    push qword [r14 + 0x88]  ; RFLAGS
    push qword [r14 + 0x90]  ; CS
    push qword [r14 + 0x80]  ; RIP
    
    ; Load RDI (last register to restore)
    mov rdi, [r14 + 0x28]
      
    ; Return to new context
    iretq

.done:
    ; Clean stack and return
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
