global switch_context_x64
global iretq_checkpoint_buf

section .bss
align 16
iretq_checkpoint_buf:
    resq 40

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
    test rsi, rsi
    jz .skip

    ; rdi=NULL: user IRQ frame already in prev->task (skip clobbering save).
    test rdi, rdi
    jz .load_only

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

.load_only:
    ; ---- Load new context (rsi = &new->task) ----

    mov r11, rsi

    sub rsp, 8
    mov rax, [r11 + 0xB0]
    mov [rsp], rax

    mov rax, [r11 + 0x00]
    mov rbx, [r11 + 0x08]
    mov rcx, [r11 + 0x10]
    mov rdx, [r11 + 0x18]
    mov rsi, [r11 + 0x20]
    mov rbp, [r11 + 0x78]
    mov r8,  [r11 + 0x30]
    mov r9,  [r11 + 0x38]
    mov r10, [r11 + 0x40]
    mov r12, [r11 + 0x50]
    mov r13, [r11 + 0x58]
    mov r14, [r11 + 0x60]
    mov r15, [r11 + 0x68]

    ; FASE 18: restore IA32_FS_BASE from process_t->fs_base (offset 0x258).
    ; task is at offset 0 of process_t, so r11 (=&task) doubles as process_t*.
    ; Guarded by _Static_assert in kernel/process.h.
    push rax
    push rcx
    push rdx
    mov rax, [r11 + 0x258]
    mov rdx, rax
    shr rdx, 32
    mov ecx, 0xC0000100
    wrmsr
    pop rdx
    pop rcx
    pop rax

    ; Build iretq frame on kernel CR3; segments reload on iretq (SS) / user entry.
    movzx rdi, word [r11 + 0x9A]
    push rdi
    push qword [r11 + 0x70]
    push qword [r11 + 0x88]
    movzx rdi, word [r11 + 0x90]
    push rdi
    push qword [r11 + 0x80]

    mov rdi, [r11 + 0x28]
    mov r11, [r11 + 0x48]

    mov rax, [rsp + 40]
    mov cr3, rax
    iretq

.skip:
    ret

;
; arch_switch_to_user_task_asm(const task_t *task)
; Linux ret_from_fork-style entry: load CR3 + GPRs, iretq (no ring-0 MOV SS).
;
global arch_switch_to_user_task_asm
arch_switch_to_user_task_asm:
    test rdi, rdi
    jz .aus_ret

    mov r11, rdi

    ; --- CHECKPOINT A: snapshot intended iretq frame from task_t ---
    push rax
    push rcx
    lea rax, [rel iretq_checkpoint_buf]
    mov qword [rax + 0], 0xAAA1
    mov rcx, [r11 + 0xB0]
    mov [rax + 8], rcx
    mov rcx, [r11 + 0x80]
    mov [rax + 16], rcx
    movzx rcx, word [r11 + 0x90]
    mov [rax + 24], rcx
    mov rcx, [r11 + 0x88]
    or rcx, 2
    mov [rax + 32], rcx
    mov rcx, [r11 + 0x70]
    mov [rax + 40], rcx
    movzx rcx, word [r11 + 0x9A]
    mov [rax + 48], rcx
    mov [rax + 56], r11
    pop rcx
    pop rax
    ; -------------------------

    sub rsp, 8
    mov rax, [r11 + 0xB0]
    mov [rsp], rax
    mov cr3, rax

    mov rax, [r11 + 0x00]
    mov rbx, [r11 + 0x08]
    mov rcx, [r11 + 0x10]
    mov rdx, [r11 + 0x18]
    mov rsi, [r11 + 0x20]
    mov rbp, [r11 + 0x78]
    mov r8,  [r11 + 0x30]
    mov r9,  [r11 + 0x38]
    mov r10, [r11 + 0x40]
    mov r12, [r11 + 0x50]
    mov r13, [r11 + 0x58]
    mov r14, [r11 + 0x60]
    mov r15, [r11 + 0x68]

    ; --- FASE 9A: capture rax around DS/ES load (no new regs, no CR3 touch) ---
    push rax
    push rcx
    lea rax, [rel iretq_checkpoint_buf]
    mov qword [rax + 256], 0xEEE1
    mov rcx, [r11 + 0x00]
    mov [rax + 264], rcx              ; ckpt_task_rax = task->rax
    mov rcx, [rsp + 8]                ; rax saved on stack (pre DS/ES load)
    mov [rax + 272], rcx              ; ckpt_rax_before_ds
    pop rcx
    pop rax
    ; -------------------------

    mov dx, 0x23
    mov ds, dx
    mov es, dx

    ; --- FASE 9A POST: capture rax AFTER segment load (now via dx) ---
    push rax
    push rcx
    lea rax, [rel iretq_checkpoint_buf]
    mov rcx, [rsp + 8]                ; rax saved on stack (post DS/ES load)
    mov [rax + 280], rcx              ; ckpt_rax_after_ds
    pop rcx
    pop rax
    ; -------------------------

    ; FASE 18: restore IA32_FS_BASE from process_t->fs_base (offset 0x258).
    ; task is at offset 0 of process_t, so r11 doubles as process_t*.
    ; Guarded by _Static_assert in kernel/process.h.
    push rax
    push rcx
    push rdx
    mov rax, [r11 + 0x258]
    mov rdx, rax
    shr rdx, 32
    mov ecx, 0xC0000100
    wrmsr
    pop rdx
    pop rcx
    pop rax

    movzx rdi, word [r11 + 0x9A]
    push rdi
    push qword [r11 + 0x70]
    mov rdi, [r11 + 0x88]
    or rdi, 2
    push rdi
    movzx rdi, word [r11 + 0x90]
    push rdi
    push qword [r11 + 0x80]

    mov rdi, [r11 + 0x28]
    mov r11, [r11 + 0x48]

    ; --- CHECKPOINT B: snapshot stack-built iretq frame just before iretq ---
    push rax
    push rcx
    lea rax, [rel iretq_checkpoint_buf]
    mov qword [rax + 64], 0xBBB1
    mov rcx, [rsp + 16 + 0]
    mov [rax + 72], rcx
    mov rcx, [rsp + 16 + 8]
    mov [rax + 80], rcx
    mov rcx, [rsp + 16 + 16]
    mov [rax + 88], rcx
    mov rcx, [rsp + 16 + 24]
    mov [rax + 96], rcx
    mov rcx, [rsp + 16 + 32]
    mov [rax + 104], rcx
    mov rcx, [rsp + 16 + 40]
    mov [rax + 112], rcx
    pop rcx
    pop rax
    ; -------------------------

    ; --- FASE 6A PRE: read iretq frame slots BEFORE mov cr3 ---
    push rax
    push rcx
    lea rax, [rel iretq_checkpoint_buf]
    mov qword [rax + 128], 0xCCC1
    mov rcx, [rsp + 16 + 0]
    mov [rax + 136], rcx
    mov rcx, [rsp + 16 + 8]
    mov [rax + 144], rcx
    mov rcx, [rsp + 16 + 16]
    mov [rax + 152], rcx
    mov rcx, [rsp + 16 + 24]
    mov [rax + 160], rcx
    mov rcx, [rsp + 16 + 32]
    mov [rax + 168], rcx
    pop rcx
    pop rax
    ; -------------------------

    ; --- FASE 6A POST: read same slots AFTER mov cr3 (no rsp change) ---
    push rax
    push rcx
    lea rax, [rel iretq_checkpoint_buf]
    mov qword [rax + 176], 0xDDD1
    mov rcx, [rsp + 16 + 0]
    mov [rax + 184], rcx
    mov rcx, [rsp + 16 + 8]
    mov [rax + 192], rcx
    mov rcx, [rsp + 16 + 16]
    mov [rax + 200], rcx
    mov rcx, [rsp + 16 + 24]
    mov [rax + 208], rcx
    mov rcx, [rsp + 16 + 32]
    mov [rax + 216], rcx
    pop rcx
    pop rax
    ; -------------------------

    iretq

.aus_ret:
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
