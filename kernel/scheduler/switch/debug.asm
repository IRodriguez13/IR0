; Debug utilities for context switching

extern serial_print
extern serial_print_hex64

; Save all registers to stack
%macro SAVE_REGS 0
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
%endmacro

; Restore all registers from stack
%macro RESTORE_REGS 0
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
%endmacro

; Print register value
%macro PRINT_REG 2
    push rdi
    push rsi
    push rdx
    push rcx
    push rax
    
    lea rdi, [%1_msg]
    call serial_print
    
    mov rdi, %2
    call serial_print_hex64
    
    lea rdi, [newline]
    call serial_print
    
    pop rax
    pop rcx
    pop rdx
    pop rsi
    pop rdi
%endmacro

section .rodata
cr3_msg: db "CR3: ", 0
rip_msg: db "RIP: ", 0
rsp_msg: db "RSP: ", 0
rbp_msg: db "RBP: ", 0
newline: db 0x0A, 0x00

section .text

; void debug_print_context(const char* msg, task_t* task)
global debug_print_context
debug_print_context:
    SAVE_REGS
    
    ; Save message
    mov r15, rdi  ; message
    mov r14, rsi  ; task
    
    ; Print message
    mov rdi, r15
    call serial_print
    
    ; Print PID if available
    test r14, r14
    jz .no_task
    
    mov rdi, [r14 + 0x28]  ; task.pid
    call serial_print_hex64
    
    mov rdi, newline
    call serial_print
    
    ; Print important registers
    PRINT_REG cr3, [r14 + 0xB0]  ; task.cr3
    PRINT_REG rip, [r14 + 0x80]  ; task.rip
    PRINT_REG rsp, [r14 + 0x70]  ; task.rsp
    PRINT_REG rbp, [r14 + 0x78]  ; task.rbp
    
.no_task:
    RESTORE_REGS
    ret
