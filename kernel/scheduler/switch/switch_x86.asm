; kernel/scheduler/switch/switch_x86.asm - Real Context Switching for x86-32
; This implements actual context switching in assembly for x86-32 architecture

global switch_context_x86
global switch_to_user_mode_x86
global switch_task

extern current_task
extern next_task

section .text

; ===============================================================================
; CONTEXT SWITCHING FUNCTION
; ===============================================================================
; void switch_context_x86(task_t *current, task_t *next)
; Switches from current task to next task, preserving all CPU state

switch_context_x86:
    ; Function prologue
    push ebp
    mov ebp, esp
    
    ; Save current task state
    ; [ebp+8] = current task, [ebp+12] = next task
    mov eax, [ebp+8]         ; Load current task pointer
    mov ebx, [ebp+12]        ; Load next task pointer
    
    ; Save general purpose registers
    mov [eax + 0x00], eax    ; Save EAX
    mov [eax + 0x04], ebx    ; Save EBX
    mov [eax + 0x08], ecx    ; Save ECX
    mov [eax + 0x0C], edx    ; Save EDX
    mov [eax + 0x10], esi    ; Save ESI
    mov [eax + 0x14], edi    ; Save EDI
    
    ; Save stack pointer
    mov [eax + 0x18], esp    ; Save ESP
    
    ; Save base pointer
    mov [eax + 0x1C], ebp    ; Save EBP
    
    ; Save instruction pointer (return address)
    mov ecx, [esp + 4]       ; Get return address
    mov [eax + 0x20], ecx    ; Save EIP
    
    ; Save flags
    pushfd                   ; Push flags
    pop ecx                  ; Pop flags into ECX
    mov [eax + 0x24], ecx    ; Save EFLAGS
    
    ; Save segment registers
    mov cx, cs
    mov [eax + 0x28], cx     ; Save CS
    mov cx, ds
    mov [eax + 0x2A], cx     ; Save DS
    mov cx, es
    mov [eax + 0x2C], cx     ; Save ES
    mov cx, fs
    mov [eax + 0x2E], cx     ; Save FS
    mov cx, gs
    mov [eax + 0x30], cx     ; Save GS
    mov cx, ss
    mov [eax + 0x32], cx     ; Save SS
    
    ; Save control registers
    mov ecx, cr0
    mov [eax + 0x34], ecx    ; Save CR0
    mov ecx, cr2
    mov [eax + 0x38], ecx    ; Save CR2
    mov ecx, cr3
    mov [eax + 0x3C], ecx    ; Save CR3
    
    ; Save debug registers
    mov ecx, dr0
    mov [eax + 0x40], ecx    ; Save DR0
    mov ecx, dr1
    mov [eax + 0x44], ecx    ; Save DR1
    mov ecx, dr2
    mov [eax + 0x48], ecx    ; Save DR2
    mov ecx, dr3
    mov [eax + 0x4C], ecx    ; Save DR3
    mov ecx, dr6
    mov [eax + 0x50], ecx    ; Save DR6
    mov ecx, dr7
    mov [eax + 0x54], ecx    ; Save DR7
    
    ; Switch to next task
    ; ebx contains the next task pointer
    mov eax, ebx             ; Move next task to eax for restoration
    
    ; Restore control registers
    mov ecx, [eax + 0x3C]    ; Load CR3 (page directory)
    mov cr3, ecx             ; Restore CR3
    
    ; Restore segment registers
    mov cx, [eax + 0x32]     ; Load SS
    mov ss, cx               ; Restore SS
    mov cx, [eax + 0x2A]     ; Load DS
    mov ds, cx               ; Restore DS
    mov cx, [eax + 0x2C]     ; Load ES
    mov es, cx               ; Restore ES
    mov cx, [eax + 0x2E]     ; Load FS
    mov fs, cx               ; Restore FS
    mov cx, [eax + 0x30]     ; Load GS
    mov gs, cx               ; Restore GS
    
    ; Restore general purpose registers
    mov ebx, [eax + 0x04]    ; Restore EBX
    mov ecx, [eax + 0x08]    ; Restore ECX
    mov edx, [eax + 0x0C]    ; Restore EDX
    mov esi, [eax + 0x10]    ; Restore ESI
    mov edi, [eax + 0x14]    ; Restore EDI
    
    ; Restore stack pointer
    mov esp, [eax + 0x18]    ; Restore ESP
    
    ; Restore base pointer
    mov ebp, [eax + 0x1C]    ; Restore EBP
    
    ; Restore flags
    mov ecx, [eax + 0x24]    ; Load EFLAGS
    push ecx                 ; Push flags
    popfd                    ; Restore EFLAGS
    
    ; Restore instruction pointer
    mov ecx, [eax + 0x20]    ; Load EIP
    push ecx                 ; Push return address
    
    ; Restore EAX (must be last as we're using it)
    mov eax, [eax + 0x00]    ; Restore EAX
    
    ; Return to the new task
    ret

; ===============================================================================
; USER MODE SWITCHING FUNCTION
; ===============================================================================
; void switch_to_user_mode_x86(void *user_stack, void *user_entry)
; Switches from kernel mode to user mode

switch_to_user_mode_x86:
    ; Function prologue
    push ebp
    mov ebp, esp
    
    ; [ebp+8] = user stack, [ebp+12] = user entry point
    mov eax, [ebp+8]         ; Load user stack
    mov ebx, [ebp+12]        ; Load user entry point
    
    ; Set up user mode stack
    mov esp, eax             ; Set user stack pointer
    
    ; Set up segment registers for user mode
    mov ax, 0x23             ; User data segment (0x23 = 35 = 4*8+3)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Clear flags (disable interrupts, etc.)
    cli                      ; Disable interrupts
    cld                      ; Clear direction flag
    
    ; Set up return to user mode
    push 0x23                ; User data segment
    push eax                 ; User stack pointer
    push 0x202               ; EFLAGS (IF=1, IOPL=0)
    push 0x1B                ; User code segment (0x1B = 27 = 3*8+3)
    push ebx                 ; User entry point
    
    ; Return to user mode
    iret

; ===============================================================================
; TASK SWITCHING WITH TSS
; ===============================================================================
; void switch_task_tss_x86(task_t *next_task)
; Switches tasks using TSS (Task State Segment)

switch_task_tss_x86:
    ; Function prologue
    push ebp
    mov ebp, esp
    
    ; [ebp+8] = next task pointer
    mov eax, [ebp+8]         ; Load next task pointer
    
    ; Load TSS selector
    mov ax, [eax + 0x58]     ; Load TSS selector from task structure
    ltr ax                    ; Load task register
    
    ; Jump to the task
    jmp far [eax + 0x50]     ; Far jump to task entry point
    
    ; This should never return
    ret

; ===============================================================================
; INTERRUPT CONTEXT SWITCHING
; ===============================================================================
; void interrupt_context_switch_x86(void)
; Performs context switching from interrupt context

interrupt_context_switch_x86:
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

; void save_fpu_state_x86(void *fpu_state)
; Saves FPU state
save_fpu_state_x86:
    ; [esp+4] = pointer to FPU state buffer
    mov eax, [esp+4]         ; Load FPU state buffer pointer
    fnsave [eax]             ; Save FPU state
    ret

; void restore_fpu_state_x86(void *fpu_state)
; Restores FPU state
restore_fpu_state_x86:
    ; [esp+4] = pointer to FPU state buffer
    mov eax, [esp+4]         ; Load FPU state buffer pointer
    frstor [eax]             ; Restore FPU state
    ret

; void invalidate_tlb_x86(void)
; Invalidates the TLB (Translation Lookaside Buffer)
invalidate_tlb_x86:
    mov eax, cr3             ; Load CR3
    mov cr3, eax             ; Write back CR3 (invalidates TLB)
    ret

; void flush_cache_x86(void)
; Flushes the CPU cache
flush_cache_x86:
    wbinvd                   ; Write back and invalidate cache
    ret

; ===============================================================================
; COMPATIBILITY ALIAS
; ===============================================================================
; Alias for compatibility with C code
switch_task:
    jmp switch_context_x86