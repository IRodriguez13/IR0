; boot_x64.asm - Bootloader Multiboot1 para x86-64
BITS 32                     ; GRUB nos deja en modo 32-bit protegido

; Multiboot1 header
section .multiboot
align 4
multiboot_header:
    dd 0x1BADB002          ; Magic number
    dd 0x00000000          ; Flags (no special requirements)
    dd -(0x1BADB002 + 0x00000000) ; Checksum


section .text
global _start
extern kmain_x64

_start:
    ; Configurar stack
    mov esp, stack_top

    ; Verificar multiboot
    cmp eax, 0x2BADB002    ; Magic number de multiboot
    jne .no_multiboot

    ; Configurar paginación
    call setup_paging

    ; Habilitar PAE
    mov eax, cr4
    or eax, 1 << 5        ; PAE bit
    mov cr4, eax

    ; Configurar EFER para long mode
    mov ecx, 0xC0000080   ; EFER MSR
    rdmsr
    or eax, 1 << 8        ; Long Mode Enable
    wrmsr

    ; Habilitar paginación
    mov eax, cr0
    or eax, 1 << 31       ; Paging Enable
    mov cr0, eax

    ; Cargar GDT 64-bit
    lgdt [gdt64_descriptor]

    ; Saltar a modo 64-bit
    jmp 0x08:long_mode

.no_multiboot:
    mov dword [0xb8000], 0x4f524f45  ; "ER" en rojo
    mov dword [0xb8004], 0x4f3a4f52  ; "R:" en rojo
    cli
    hlt
    jmp .no_multiboot

; ===============================================================================
; Modo 64-bit
; ===============================================================================
BITS 64
long_mode:
    ; Configurar segmentos de datos
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Configurar stack 64-bit
    mov rsp, 0x90000

    ; Log entrada a 64-bit
    mov rax, 0x0f380f38    ; "88" en blanco sobre azul
    mov [0xb8000], rax

    ; Llamar al kernel
    call kmain_x64

    ; Si el kernel retorna, halt
.halt:
    cli
    hlt
    jmp .halt

; ===============================================================================
; Configuración de paginación
; ===============================================================================
BITS 32
setup_paging:
    ; Limpiar tablas de páginas
    mov edi, 0x1000
    mov ecx, 4096
    xor eax, eax
    rep stosd

    ; Configurar PML4
    mov eax, 0x2003       ; PDPT address + present + writable
    mov [0x1000], eax

    ; Configurar PDPT
    mov eax, 0x3003       ; PD address + present + writable
    mov [0x2000], eax

    ; Configurar PD (2MB pages)
    mov edi, 0x3000
    mov ecx, 0
.loop:
    mov eax, ecx
    shl eax, 21           ; 2MB por entrada
    or eax, 0x83          ; Present + writable + huge page
    mov [edi + ecx * 8], eax
    mov dword [edi + ecx * 8 + 4], 0
    inc ecx
    cmp ecx, 512          ; 1GB de memoria
    jb .loop

    ; Cargar CR3
    mov eax, 0x1000
    mov cr3, eax

    ret

; ===============================================================================
; Datos
; ===============================================================================
section .bss
align 16
stack_bottom:
    resb 16384              ; 16KB de stack
stack_top:

section .data
; GDT para modo 64-bit
gdt64:
    dq 0x0000000000000000  ; Null descriptor
    dq 0x00AF9A000000FFFF  ; Code 64-bit descriptor
    dq 0x00AF92000000FFFF  ; Data 64-bit descriptor
gdt64_end:

gdt64_descriptor:
    dw gdt64_end - gdt64 - 1
    dq gdt64