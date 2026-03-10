; SPDX-License-Identifier: GPL-3.0-only
;/**
; * IR0 Kernel — Core system software
; * Copyright (C) 2025  Iván Rodriguez
; *
; * This file is part of the IR0 Operating System.
; * Distributed under the terms of the GNU General Public License v3.0.
; * See the LICENSE file in the project root for full license information.
; *
; * File: boot_x64.asm
; * Description: x86-64 boot loader with long mode setup, paging, and kernel entry
; */

; Multiboot header
; flags bit 2 = request graphics mode (mode_type, width, height, depth)

section .multiboot
align 4
    dd 0x1BADB002               ; Magic
    dd 0x04                     ; Flags: bit 2 = graphics mode
    dd -(0x1BADB002 + 0x04)      ; Checksum
    dd 0                         ; mode_type: 0 = linear graphics
    dd 1024                      ; width (pixels)
    dd 768                       ; height (pixels)
    dd 32                        ; depth (bpp)


; Stack

section .bss
align 16
stack_bottom:
    resb 16384                  ; 16KB stack
stack_top:

section .text

[BITS 32]

global _start
extern kmain

; Main text segment
_start:
    ; Multiboot check
    cmp eax, 0x2BADB002
    jne .no_multiboot

    ; Initial Stack 
    mov esp, stack_top

    ; PAE on

    mov eax, cr4
    or eax, 1 << 5              ; CR4.PAE
    mov cr4, eax

    ; Long Mode (LME)

    mov ecx, 0xC0000080         ; IA32_EFER MSR
    rdmsr
    or eax, 1 << 8              ; bit LME
    wrmsr

    ; Config CR3

    mov eax, pml4_minimal
    mov cr3, eax

    ; Paging on

    mov eax, cr0
    or eax, 1 << 31             ; CR0.PG
    mov cr0, eax

    ; GDT load before to replace it in c code.

    lgdt [gdt_descriptor]

    ; Salto far a 64-bit
    jmp CODE_SEL:modo_64bit

.no_multiboot: ; panic if multiboot crashes
    mov dword [0xB8000], 0x4F4E4F4D  ; "MN"
    cli
    hlt

; 64-bit code
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

    ; Stack in mapped range
    mov rsp, 0x8FF00

    ; Pass multiboot info (ebx) to kmain as first arg (rdi)
    mov edi, ebx

    ; jump to kernel
    call kmain

.halt: ; primitive panic if returns
    cli
    hlt
    jmp .halt

; page tables in early boot
; Framebuffer: GRUB puts it at 0xFD000000 (e.g. 1280x800x32 = 4MB).
; 0xFD000000: PML4=0, PDP=3, PD=0x1E8. Map 2x2MB pages.
section .data
align 4096
pml4_minimal:
    dq pdp_minimal + 0x7   ; Present + RW + User
    times 511 dq 0

align 4096
pdp_minimal:
    dq pd_minimal + 0x7   ; Present + RW + User (0-2MB)
    dq 0
    dq 0
    dq pd_fb + 0x7        ; Present + RW + User (framebuffer 0xFD000000, PDP idx 3)
    times 508 dq 0

align 4096
pd_fb:
    times 0x1E8 dq 0
    dq 0xFD000000 + 0x87   ; 2MB page at 0xFD000000
    dq 0xFD200000 + 0x87   ; 2MB page at 0xFD200000 (4MB total)
    times (511 - 0x1E9) dq 0

align 4096
pd_minimal:
    
    ; 0x87 = Present (1) + RW (1) + User (1) + PS (1) = 10000111
    dq 0x000000 + 0x87
    dq 0x200000 + 0x87
    dq 0x400000 + 0x87
    dq 0x600000 + 0x87
    dq 0x800000 + 0x87
    dq 0xA00000 + 0x87
    dq 0xC00000 + 0x87
    dq 0xE00000 + 0x87
    dq 0x1000000 + 0x87
    dq 0x1200000 + 0x87
    dq 0x1400000 + 0x87
    dq 0x1600000 + 0x87
    dq 0x1800000 + 0x87
    dq 0x1A00000 + 0x87
    dq 0x1C00000 + 0x87
    dq 0x1E00000 + 0x87
    times 495 dq 0


align 16

gdt_start:
    dq 0x0000000000000000        ; Null
    dq 0x00AF9A000000FFFF        ; Code 64-bit (base=0, limit=FFFFF, gran=4K)
    dq 0x00AF92000000FFFF        ; Data 64-bit (base=0, limit=FFFFF, gran=4K)
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dq gdt_start

; Selectors
CODE_SEL equ 0x08
DATA_SEL equ 0x10
; ===============================================================================
; GNU STACK SECTION - Prevents executable stack warning
; ===============================================================================
section .note.GNU-stack noalloc noexec nowrite progbits