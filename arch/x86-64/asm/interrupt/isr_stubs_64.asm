; Stubs de interrupción para x86-64
; Cada stub guarda el número de interrupción y llama al handler común

BITS 64

extern isr_handler64
extern iretq_checkpoint_buf

section .bss
align 8
global fase28_isr_rsp_after_push
global fase28_isr_frame_ptr
global fase28_saved_rip
global fase28_saved_rsp
global isr_abi_entry_intno
global isr_abi_entry_has_err
global isr_abi_entry_rsp
global isr_abi_entry_qwords
fase28_isr_rsp_after_push:  resq 1
fase28_isr_frame_ptr:       resq 1
fase28_saved_rip:           resq 1
fase28_saved_rsp:           resq 1
isr_abi_entry_intno:        resq 1
isr_abi_entry_has_err:      resq 1
isr_abi_entry_rsp:          resq 1
isr_abi_entry_qwords:       resq 16

section .data
align 8
isr_abi_saved_rax: dq 0
isr_abi_saved_rcx: dq 0

section .text

%macro ISR_ABI_SNAPSHOT_ENTRY 2
    mov [rel isr_abi_saved_rax], rax
    mov [rel isr_abi_saved_rcx], rcx
    mov qword [rel isr_abi_entry_intno], %1
    mov qword [rel isr_abi_entry_has_err], %2
    mov [rel isr_abi_entry_rsp], rsp
    mov rcx, rsp
    mov rax, [rcx + 0]
    mov [rel isr_abi_entry_qwords + 0], rax
    mov rax, [rcx + 8]
    mov [rel isr_abi_entry_qwords + 8], rax
    mov rax, [rcx + 16]
    mov [rel isr_abi_entry_qwords + 16], rax
    mov rax, [rcx + 24]
    mov [rel isr_abi_entry_qwords + 24], rax
    mov rax, [rcx + 32]
    mov [rel isr_abi_entry_qwords + 32], rax
    mov rax, [rcx + 40]
    mov [rel isr_abi_entry_qwords + 40], rax
    mov rax, [rcx + 48]
    mov [rel isr_abi_entry_qwords + 48], rax
    mov rax, [rcx + 56]
    mov [rel isr_abi_entry_qwords + 56], rax
    mov rax, [rcx + 64]
    mov [rel isr_abi_entry_qwords + 64], rax
    mov rax, [rcx + 72]
    mov [rel isr_abi_entry_qwords + 72], rax
    mov rax, [rcx + 80]
    mov [rel isr_abi_entry_qwords + 80], rax
    mov rax, [rcx + 88]
    mov [rel isr_abi_entry_qwords + 88], rax
    mov rax, [rcx + 96]
    mov [rel isr_abi_entry_qwords + 96], rax
    mov rax, [rcx + 104]
    mov [rel isr_abi_entry_qwords + 104], rax
    mov rax, [rcx + 112]
    mov [rel isr_abi_entry_qwords + 112], rax
    mov rax, [rcx + 120]
    mov [rel isr_abi_entry_qwords + 120], rax
    mov rax, [rel isr_abi_saved_rax]
    mov rcx, [rel isr_abi_saved_rcx]
%endmacro

; Macro para stubs sin código de error
%macro ISR_NOERRCODE 1
global isr%1_64
isr%1_64:
    cli                     ; Deshabilitar interrupciones
    ISR_ABI_SNAPSHOT_ENTRY %1, 0
    push qword 0            ; Código de error dummy
    push qword %1           ; Número de interrupción
    jmp isr_common_stub_64  ; Ir al handler común
%endmacro

; Macro para stubs con código de error
%macro ISR_ERRCODE 1
global isr%1_64
isr%1_64:
    cli                     ; Deshabilitar interrupciones
    ISR_ABI_SNAPSHOT_ENTRY %1, 1
    push qword %1           ; Número de interrupción (el código de error ya está en el stack)
    jmp isr_common_stub_64  ; Ir al handler común
%endmacro

; Generar stubs para excepciones (0-31)
ISR_NOERRCODE 0   ; Divide Error
ISR_NOERRCODE 1   ; Debug Exception
ISR_NOERRCODE 2   ; Non-maskable Interrupt
ISR_NOERRCODE 3   ; Breakpoint
ISR_NOERRCODE 4   ; Overflow
ISR_NOERRCODE 5   ; Bound Range Exceeded
ISR_NOERRCODE 6   ; Invalid Opcode
ISR_NOERRCODE 7   ; Device Not Available
ISR_ERRCODE 8     ; Double Fault
ISR_NOERRCODE 9   ; Coprocessor Segment Overrun
ISR_ERRCODE 10    ; Invalid TSS
ISR_ERRCODE 11    ; Segment Not Present
ISR_ERRCODE 12    ; Stack Segment Fault
ISR_ERRCODE 13    ; General Protection Fault
ISR_ERRCODE 14    ; Page Fault
ISR_NOERRCODE 15  ; Reserved
ISR_NOERRCODE 16  ; x87 FPU Error
ISR_ERRCODE 17    ; Alignment Check
ISR_NOERRCODE 18  ; Machine Check
ISR_NOERRCODE 19  ; SIMD FPU Exception
ISR_NOERRCODE 20  ; Virtualization Exception
ISR_NOERRCODE 21  ; Reserved
ISR_NOERRCODE 22  ; Reserved
ISR_NOERRCODE 23  ; Reserved
ISR_NOERRCODE 24  ; Reserved
ISR_NOERRCODE 25  ; Reserved
ISR_NOERRCODE 26  ; Reserved
ISR_NOERRCODE 27  ; Reserved
ISR_NOERRCODE 28  ; Reserved
ISR_NOERRCODE 29  ; Reserved
ISR_NOERRCODE 30  ; Reserved
ISR_NOERRCODE 31  ; Reserved

; Generar stubs para IRQs (32-47)
ISR_NOERRCODE 32  ; Timer
ISR_NOERRCODE 33  ; Keyboard
ISR_NOERRCODE 34  ; Cascade
ISR_NOERRCODE 35  ; COM2
ISR_NOERRCODE 36  ; COM1
ISR_NOERRCODE 37  ; LPT2
ISR_NOERRCODE 38  ; Floppy
ISR_NOERRCODE 39  ; LPT1
ISR_NOERRCODE 40  ; CMOS
ISR_NOERRCODE 41  ; Free
ISR_NOERRCODE 42  ; Free
ISR_NOERRCODE 43  ; Free
ISR_NOERRCODE 44  ; PS2
ISR_NOERRCODE 45  ; FPU
ISR_NOERRCODE 46  ; ATA1
ISR_NOERRCODE 47  ; ATA2

; Stub para syscall (interrupción 0x80)
global isr128_64
extern syscall_entry_asm
isr128_64:
    jmp syscall_entry_asm   

; Handler común para todas las interrupciones
isr_common_stub_64:
    ; Guardar todos los registros
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; FASE 28: earliest trap-frame snapshot after final GPR push.
    ; Layout from current rsp:
    ;   +0..+112  = saved GPRs (r15..rax)
    ;   +120      = int_no
    ;   +128      = errcode
    ;   +136      = saved RIP
    ;   +160      = saved RSP (only valid on CPL3->CPL0 transitions)
    mov [rel fase28_isr_rsp_after_push], rsp
    lea rax, [rsp + 120]
    mov [rel fase28_isr_frame_ptr], rax
    mov rax, [rsp + 136]
    mov [rel fase28_saved_rip], rax
    mov rax, [rsp + 160]
    mov [rel fase28_saved_rsp], rax

    ; Llamar al handler C
    mov rdi, qword [rsp + 120]  ; Obtener número de interrupción del stack
    lea rsi, [rsp + 120]        ; Pasar puntero a [int_no, errcode, ...]
    call isr_handler64

    ; Restaurar todos los registros
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax

    ; Limpiar código de error y número de interrupción
    add rsp, 16

    ; Returning to ring 3: normalize SS on iretq frame (sysret uses 0x13).
    ; Frame at rsp: RIP, CS, RFLAGS, RSP, SS (+0, +8, +16, +24, +32).
    test byte [rsp + 8], 3
    jz .kernel_iret

    ; --- FASE 16 PRE: snapshot rax immediately after pop rax, before DS/ES ---
    push rax
    push rcx
    lea rcx, [rel iretq_checkpoint_buf]
    mov qword [rcx + 288], 0x16A1
    mov rax, [rsp + 8]
    mov [rcx + 296], rax
    pop rcx
    pop rax
    ; -------------------------

    mov word [rsp + 32], 0x23
    ; FASE 16 FIX: preserve rax across DS/ES scratch load (otherwise mov ax,0x23
    ; clobbers the low16 of the user-visible rax saved/restored by ISR).
    push rax
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    pop rax

    ; --- FASE 16 POST: snapshot rax immediately before iretq ---
    push rax
    push rcx
    lea rcx, [rel iretq_checkpoint_buf]
    mov qword [rcx + 304], 0x16A2
    mov rax, [rsp + 8]
    mov [rcx + 312], rax
    pop rcx
    pop rax
    ; -------------------------
.kernel_iret:

    ; Habilitar interrupciones y retornar
    sti
    iretq
; ===============================================================================
; GNU STACK SECTION - Prevents executable stack warning
; ===============================================================================
section .note.GNU-stack noalloc noexec nowrite progbits