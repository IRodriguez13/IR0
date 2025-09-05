; ===========================================================================
; boot_x64.asm - Arranque mínimo para x86-64 con Multiboot
; ===========================================================================
; Hace SOLO lo indispensable:
;  - Verifica Multiboot
;  - Configura stack inicial
;  - Configura paginación mínima (32 MiB, 2MiB pages)
;  - Habilita Long Mode
;  - Carga GDT mínima
;  - Salta a modo 64-bit y llama a kmain_x64
; ===========================================================================

; --------------------------
; Multiboot header
; --------------------------
section .multiboot
align 4
    dd 0x1BADB002               ; Magic
    dd 0x00                     ; Flags
    dd -(0x1BADB002 + 0x00)     ; Checksum

; --------------------------
; Stack
; --------------------------
section .bss
align 16
stack_bottom:
    resb 16384                  ; 16KB stack
stack_top:

; --------------------------
; Código principal (32-bit)
; --------------------------
section .text

[BITS 32]

global _start
extern kmain_x64

_start:
    ; Multiboot check
    cmp eax, 0x2BADB002
    jne .no_multiboot

    ; Stack inicial
    mov esp, stack_top

    ; --------------------------
    ; Habilitar PAE
    ; --------------------------
    mov eax, cr4
    or eax, 1 << 5              ; CR4.PAE
    mov cr4, eax

    ; --------------------------
    ; Habilitar Long Mode (LME)
    ; --------------------------
    mov ecx, 0xC0000080         ; IA32_EFER MSR
    rdmsr
    or eax, 1 << 8              ; bit LME
    wrmsr

    ; --------------------------
    ; Configurar CR3
    ; --------------------------
    mov eax, pml4_minimal
    mov cr3, eax

    ; --------------------------
    ; Habilitar Paging
    ; --------------------------
    mov eax, cr0
    or eax, 1 << 31             ; CR0.PG
    mov cr0, eax

    ; --------------------------
    ; Cargar GDT mínima
    ; --------------------------
    lgdt [gdt_descriptor]

    ; --------------------------
    ; Salto far a 64-bit
    ; --------------------------
    jmp CODE_SEL:modo_64bit

.no_multiboot:
    mov dword [0xB8000], 0x4F4E4F4D  ; "MN"
    cli
    hlt

; --------------------------
; 64-bit code
; --------------------------
[BITS 64]
modo_64bit:
    ; Segments
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor ax, ax
    mov fs, ax
    mov gs, ax

    ; Stack en rango mapeado
    mov rsp, 0x8FF00

    ; Call a C
    call kmain_x64

.halt:
    cli
    hlt
    jmp .halt

; ===========================================================================
; Tablas de paginación mínimas
; ===========================================================================
section .data
align 4096
pml4_minimal:
    dq pdp_minimal + 0x3
    times 511 dq 0

align 4096
pdp_minimal:
    dq pd_minimal + 0x3
    times 511 dq 0

align 4096
pd_minimal:
    ; 16 entradas de 2 MiB -> 32 MiB mapeados
    dq 0x000000 + 0x83
    dq 0x200000 + 0x83
    dq 0x400000 + 0x83
    dq 0x600000 + 0x83
    dq 0x800000 + 0x83
    dq 0xA00000 + 0x83
    dq 0xC00000 + 0x83
    dq 0xE00000 + 0x83
    dq 0x1000000 + 0x83
    dq 0x1200000 + 0x83
    dq 0x1400000 + 0x83
    dq 0x1600000 + 0x83
    dq 0x1800000 + 0x83
    dq 0x1A00000 + 0x83
    dq 0x1C00000 + 0x83
    dq 0x1E00000 + 0x83
    times 495 dq 0

; ===========================================================================
; GDT mínima
; ===========================================================================
align 16

gdt_start:
    dq 0x0000000000000000        ; Null
    dq 0x00AF9A000000FFFF        ; Code 64-bit (base=0, limit=FFFFF, gran=4K)
    dq 0x00AF92000000FFFF        ; Data 64-bit (base=0, limit=FFFFF, gran=4K)
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dq gdt_start

; Selectores
CODE_SEL equ 0x08
DATA_SEL equ 0x10
