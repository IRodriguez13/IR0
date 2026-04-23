global switch_context_x64

section .text

;
; switch_context_x64(task_t *current, task_t *new)
;
; Save all register state into *current, load from *new, iretq to new task.
; On null pointers, returns to caller without switching.
;
; task_t offsets (from kernel/scheduler/task.h):
;   0x00 rax, 0x08 rbx, 0x10 rcx, 0x18 rdx, 0x20 rsi, 0x28 rdi,
;   0x30 r8,  0x38 r9,  0x40 r10, 0x48 r11, 0x50 r12, 0x58 r13,
;   0x60 r14, 0x68 r15, 0x70 rsp, 0x78 rbp, 0x80 rip, 0x88 rflags,
;   0x90 cs,  0x92 ds,  0x94 es,  0x96 fs,  0x98 gs,  0x9A ss,
;   0xB0 cr3
;
switch_context_x64:
    test rdi, rdi
    jz .skip
    test rsi, rsi
    jz .skip

    ; ---- Save current context (rdi = &current->task) ----

    mov [rdi + 0x00], rax
    mov [rdi + 0x08], rbx
    mov [rdi + 0x10], rcx
    mov [rdi + 0x18], rdx
    mov [rdi + 0x20], rsi
    mov [rdi + 0x28], rdi
    mov [rdi + 0x30], r8
    mov [rdi + 0x38], r9
    mov [rdi + 0x40], r10
    mov [rdi + 0x48], r11
    mov [rdi + 0x50], r12
    mov [rdi + 0x58], r13
    mov [rdi + 0x60], r14
    mov [rdi + 0x68], r15
    mov [rdi + 0x78], rbp

    ; RSP: undo the `call` return-address push to get the caller's RSP
    lea rax, [rsp + 8]
    mov [rdi + 0x70], rax

    ; RIP: return address pushed by `call`
    mov rax, [rsp]
    mov [rdi + 0x80], rax

    pushfq
    pop qword [rdi + 0x88]

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

    ; ---- Load new context (rsi = &new->task) ----

    mov r15, rsi

    mov rax, [r15 + 0xB0]
    mov cr3, rax

    mov ax, [r15 + 0x9A]
    mov ss, ax
    mov ax, [r15 + 0x92]
    mov ds, ax
    mov ax, [r15 + 0x94]
    mov es, ax
    mov ax, [r15 + 0x96]
    mov fs, ax
    mov ax, [r15 + 0x98]
    mov gs, ax

    mov rax, [r15 + 0x00]
    mov rbx, [r15 + 0x08]
    mov rcx, [r15 + 0x10]
    mov rdx, [r15 + 0x18]
    mov rsi, [r15 + 0x20]
    mov rbp, [r15 + 0x78]
    mov r8,  [r15 + 0x30]
    mov r9,  [r15 + 0x38]
    mov r10, [r15 + 0x40]
    mov r11, [r15 + 0x48]
    mov r12, [r15 + 0x50]
    mov r13, [r15 + 0x58]
    mov r14, [r15 + 0x60]

    ; Build iretq frame: SS, RSP, RFLAGS, CS, RIP
    movzx rdi, word [r15 + 0x9A]
    push rdi
    push qword [r15 + 0x70]
    push qword [r15 + 0x88]
    movzx rdi, word [r15 + 0x90]
    push rdi
    push qword [r15 + 0x80]

    mov rdi, [r15 + 0x28]
    mov r15, [r15 + 0x68]

    iretq

.skip:
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
