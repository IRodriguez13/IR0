; kernel/scheduler/switch/switch_x64.asm - Real Context Switching for x86-64
; This implements actual context switching in assembly for x86-64 architecture

global switch_context_x64
global switch_to_user_mode_x64
global switch_task

extern current_task
extern next_task

section .text

; ===============================================================================
; CONTEXT SWITCHING FUNCTION
; ===============================================================================
; void switch_context_x64(task_t *current, task_t *next)
; Switches from current task to next task, preserving all CPU state

switch_context_x64:
    ; Function prologue
    push rbp
    mov rbp, rsp

    ; Save current task state
    ; rdi = current task, rsi = next task
    
    ; Save general purpose registers
    mov qword [rdi + 0x00], rax    ; Save RAX
    mov qword [rdi + 0x08], rbx    ; Save RBX
    mov qword [rdi + 0x10], rcx    ; Save RCX
    mov qword [rdi + 0x18], rdx    ; Save RDX
    mov qword [rdi + 0x20], rsi    ; Save RSI
    mov rax, rdi
    mov qword [rdi + 0x28], rax    ; Save RDI
    mov qword [rdi + 0x30], r8     ; Save R8
    mov qword [rdi + 0x38], r9     ; Save R9
    mov qword [rdi + 0x40], r10    ; Save R10
    mov qword [rdi + 0x48], r11    ; Save R11
    mov qword [rdi + 0x50], r12    ; Save R12
    mov qword [rdi + 0x58], r13    ; Save R13
    mov qword [rdi + 0x60], r14    ; Save R14
    mov qword [rdi + 0x68], r15    ; Save R15
    
    ; Save stack pointer
    mov qword [rdi + 0x70], rsp    ; Save RSP
    
    ; Save base pointer
    mov qword [rdi + 0x78], rbp    ; Save RBP
    
    ; Save instruction pointer (return address)
    mov rax, qword [rsp + 8]       ; Get return address
    mov qword [rdi + 0x80], rax    ; Save RIP
    
    ; Save flags
    pushfq                   ; Push flags
    pop rax                  ; Pop flags into RAX
    mov qword [rdi + 0x88], rax    ; Save RFLAGS
    
    ; Save segment registers
    mov ax, cs
    mov word [rdi + 0x90], ax     ; Save CS
    mov ax, ds
    mov word [rdi + 0x92], ax     ; Save DS
    mov ax, es
    mov word [rdi + 0x94], ax     ; Save ES
    mov ax, fs
    mov word [rdi + 0x96], ax     ; Save FS
    mov ax, gs
    mov word [rdi + 0x98], ax     ; Save GS
    mov ax, ss
    mov word [rdi + 0x9A], ax     ; Save SS
    
    ; Save control registers
    mov rax, cr0
    mov qword [rdi + 0xA0], rax    ; Save CR0
    mov rax, cr2
    mov qword [rdi + 0xA8], rax    ; Save CR2
    mov rax, cr3
    mov qword [rdi + 0xB0], rax    ; Save CR3
    mov rax, cr4
    mov qword [rdi + 0xB8], rax    ; Save CR4
    
    ; Save debug registers
    mov rax, dr0
    mov qword [rdi + 0xC0], rax    ; Save DR0
    mov rax, dr1
    mov qword [rdi + 0xC8], rax    ; Save DR1
    mov rax, dr2
    mov qword [rdi + 0xD0], rax    ; Save DR2
    mov rax, dr3
    mov qword [rdi + 0xD8], rax    ; Save DR3
    mov rax, dr6
    mov qword [rdi + 0xE0], rax    ; Save DR6
    mov rax, dr7
    mov qword [rdi + 0xE8], rax    ; Save DR7
    
    ; Switch to next task
    ; rsi contains the next task pointer
    mov rdi, rsi             ; Move next task to rdi for restoration
    
    ; Restore control registers
    mov rax, qword [rdi + 0xB0]    ; Load CR3 (page directory)
    mov cr3, rax             ; Restore CR3
    
    ; Restore segment registers
    mov ax, word [rdi + 0x9A]     ; Load SS
    mov ss, ax               ; Restore SS
    mov ax, word [rdi + 0x92]     ; Load DS
    mov ds, ax               ; Restore DS
    mov ax, word [rdi + 0x94]     ; Load ES
    mov es, ax               ; Restore ES
    mov ax, word [rdi + 0x96]     ; Load FS
    mov fs, ax               ; Restore FS
    mov ax, word [rdi + 0x98]     ; Load GS
    mov gs, ax               ; Restore GS
    
    ; Restore general purpose registers
    mov rax, qword [rdi + 0x00]    ; Restore RAX
    mov rbx, qword [rdi + 0x08]    ; Restore RBX
    mov rcx, qword [rdi + 0x10]    ; Restore RCX
    mov rdx, qword [rdi + 0x18]    ; Restore RDX
    mov rsi, qword [rdi + 0x20]    ; Restore RSI
    mov r8, qword [rdi + 0x30]     ; Restore R8
    mov r9, qword [rdi + 0x38]     ; Restore R9
    mov r10, qword [rdi + 0x40]    ; Restore R10
    mov r11, qword [rdi + 0x48]    ; Restore R11
    mov r12, qword [rdi + 0x50]    ; Restore R12
    mov r13, qword [rdi + 0x58]    ; Restore R13
    mov r14, qword [rdi + 0x60]    ; Restore R14
    mov r15, qword [rdi + 0x68]    ; Restore R15
    
    ; Restore stack pointer
    mov rsp, qword [rdi + 0x70]    ; Restore RSP
    
    ; Restore base pointer
    mov rbp, qword [rdi + 0x78]    ; Restore RBP
    
    ; Restore flags
    mov rax, qword [rdi + 0x88]    ; Load RFLAGS
    push rax                 ; Push flags
    popfq                    ; Restore RFLAGS
    
    ; Restore instruction pointer
    mov rax, qword [rdi + 0x80]    ; Load RIP
    push rax                 ; Push return address
    
    ; Restore RDI (must be last as we're using it)
    mov rdi, qword [rdi + 0x28]    ; Restore RDI
    
    ; Return to the new task
    ret

; ===============================================================================
; ALIAS FOR COMPATIBILITY
; ===============================================================================

switch_task:
    ; Alias for switch_context_x64 for compatibility
    jmp switch_context_x64

; ===============================================================================
; USER MODE SWITCHING FUNCTION
; ===============================================================================
; void switch_to_user_mode_x64(void *user_stack, void *user_entry)
; Switches from kernel mode to user mode



switch_to_user_mode_x64:
    ; Function prologue
    push rbp
    mov rbp, rsp
    
    ; rdi = user stack, rsi = user entry point
    
    ; Save parameters in preserved registers
    mov r12, rdi    ; Save user stack in r12
    mov r13, rsi    ; Save user entry in r13
    
    ; Set up user mode stack
    mov rsp, r12    ; Set user stack pointer
    
    ; Set up segment registers for user mode
    ; In x86-64, user mode segments are:
    ; CS = 0x1B (27 = 3*8+3, user code segment with RPL=3)
    ; DS/ES/FS/GS/SS = 0x23 (35 = 4*8+3, user data segment with RPL=3)
    
    ; CORRECCIÓN: Usar los selectores correctos de la GDT
    ; 0x18 = User code segment (índice 3 * 8 = 24, + 0 = 24 = 0x18)
    ; 0x20 = User data segment (índice 4 * 8 = 32, + 0 = 32 = 0x20)
    ; Para RPL=3, sumamos 3: 0x18 + 3 = 0x1B, 0x20 + 3 = 0x23
    
    ; Set up data segments
    mov ax, 0x23    ; User data segment (0x20 + 3 = 0x23)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Clear flags
    cli             ; Disable interrupts
    cld             ; Clear direction flag
    
    ; Set up return to user mode
    ; Stack layout for iretq:
    ; [RSP+32] = SS (user data segment with RPL=3)
    ; [RSP+24] = RSP (user stack pointer)
    ; [RSP+16] = RFLAGS (with IF=1 for interrupts)
    ; [RSP+8]  = CS (user code segment with RPL=3)
    ; [RSP+0]  = RIP (user entry point)
    
    ; Push the iretq frame
    push 0x23       ; User data segment (SS) with RPL=3 (0x20 + 3)
    push r12        ; User stack pointer (RSP)
    push 0x202      ; RFLAGS (IF=1, IOPL=0, other bits cleared)
    push 0x1B       ; User code segment (CS) with RPL=3 (0x18 + 3)
    push r13        ; User entry point (RIP)
    
    ; Return to user mode
    iretq
    
    ; This should never be reached
    ret

; ===============================================================================
; TASK SWITCHING WITH TSS
; ===============================================================================
; void switch_task_tss_x64(task_t *next_task)
; Switches tasks using TSS (Task State Segment)

switch_task_tss_x64:
    ; Function prologue
    push rbp
    mov rbp, rsp
    
    ; rdi = next task pointer
    
    ; Load TSS selector
    mov ax, [rdi + 0xF0]     ; Load TSS selector from task structure
    ltr ax                    ; Load task register
    
    ; Jump to the task
    jmp far [rdi + 0xE8]     ; Far jump to task entry point
    
    ; This should never return
    ret

; ===============================================================================
; INTERRUPT CONTEXT SWITCHING
; ===============================================================================
; void interrupt_context_switch_x64(void)
; Performs context switching from interrupt context

interrupt_context_switch_x64:
    ; This is called from interrupt context
    ; The interrupt frame is already on the stack
    
    ; Save current task state to current_task structure
    ; This is a simplified version - in practice, you'd need to
    ; save the interrupt frame and restore it later
    
    ; For now, just return
    ret

; ===============================================================================
; UTILITY FUNCTIONS
; ===============================================================================

; void save_fpu_state_x64(void *fpu_state)
; Saves FPU/SSE state
save_fpu_state_x64:
    ; rdi = pointer to FPU state buffer
    fxsave [rdi]             ; Save FPU/SSE state
    ret

; void restore_fpu_state_x64(void *fpu_state)
; Restores FPU/SSE state
restore_fpu_state_x64:
    ; rdi = pointer to FPU state buffer
    fxrstor [rdi]            ; Restore FPU/SSE state
    ret

; void invalidate_tlb_x64(void)
; Invalidates the TLB (Translation Lookaside Buffer)
invalidate_tlb_x64:
    mov rax, cr3             ; Load CR3
    mov cr3, rax             ; Write back CR3 (invalidates TLB)
    ret

; void flush_cache_x64(void)
; Flushes the CPU cache
flush_cache_x64:
    wbinvd                   ; Write back and invalidate cache
    ret