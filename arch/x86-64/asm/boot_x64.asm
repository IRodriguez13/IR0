; boot_x64.asm - Bootloader completo para x86-64
; Hecho DESDE CERO para evitar problemas de jmp far

; Multiboot header
section .multiboot
align 4
    dd 0x1BADB002               ; Multiboot magic
    dd 0x00                     ; Flags
    dd -(0x1BADB002 + 0x00)     ; Checksum

; Stack para boot
section .bss
align 16
stack_bottom:
    resb 16384                  ; 16KB stack
stack_top:

; Código principal
section .text
global _start
extern kmain_x64

; Todo en 32-bit inicialmente
BITS 32

_start:
    ; Verificar que tenemos multiboot
    cmp eax, 0x2BADB002
    jne error_no_multiboot

    ; Configurar stack inicial
    mov esp, stack_top

    ; Verificar que estamos en modo protegido
    mov eax, cr0
    test eax, 1
    jz error_no_protected

    ; === CONFIGURACIÓN PARA LONG MODE ===

    ; 1. Habilitar PAE
    mov eax, cr4
    or eax, 1 << 5              ; PAE bit (Physical Address Extension)
    mov cr4, eax

    ; 2. Configurar EFER para Long Mode
    mov ecx, 0xC0000080         ; EFER MSR
    rdmsr
    or eax, 1 << 8              ; LME bit (Long Mode Enable)
    wrmsr

    ; 3. Cargar nuestra GDT
    lgdt [gdt_descriptor]

    ; 4. Configurar CR3 con PML4 mínimo (solo para boot)
    mov eax, pml4_minimal       ; PML4 mínimo para boot
    mov cr3, eax

    ; 5. Habilitar paging - REQUERIDO para Long Mode
    mov eax, cr0
    or eax, 1 << 31             ; PG bit
    mov cr0, eax

    ; 6. Ahora hacemos el jmp far CORRECTAMENTE
    jmp CODE_SEL:modo_64bit

; === MANEJADORES DE ERROR ===
error_no_multiboot:
    mov dword [0xB8000], 0x4F4E4F4D  ; "MN" en rojo
    mov dword [0xB8004], 0x4F544F42  ; "BT" en rojo
    cli
    hlt
    jmp $

error_no_protected:
    mov dword [0xB8000], 0x4F334F33  ; "33" en rojo
    mov dword [0xB8004], 0x4F324F32  ; "22" en rojo
    cli
    hlt
    jmp $

; === CÓDIGO 64-BIT ===
BITS 64
modo_64bit:
    ; Configurar selectores de datos para 64-bit
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Configurar stack 64-bit seguro dentro del rango mapeado
    ; Stack: 0x80000 - 0x8FFFF (64KB, alineado a 16 bytes)
    mov rsp, 0x8FF00

    ; === SALTO A KMAIN_X64 COMO LA GENTE ===
    ; NO usamos dirección hardcodeada - usamos el símbolo directamente
    call kmain_x64

    ; Si kmain_x64 retorna, detenerse
.halt_loop:
    cli
    hlt
    jmp .halt_loop

; === TABLAS MÍNIMAS PARA BOOT - SOLUCIÓN HÍBRIDA ===
section .data
align 4096
; PML4 mínimo - solo para boot
pml4_minimal:
    dq pdp_minimal + 0x3        ; Present + RW + User (PDPT)
    times 511 dq 0              ; Resto de entradas = 0

align 4096
; PDPT mínimo - solo para boot
pdp_minimal:
    dq pd_minimal + 0x3         ; Present + RW + User (PD)
    times 511 dq 0              ; Resto de entradas = 0

align 4096
; PD mínimo - kernel + expansión futura (32MB)
pd_minimal:
    dq 0x000000 + 0x83          ; 0x000000 - 0x1FFFFF (2MB) - Present + RW + 2MB
    ; Expandir para 32MB (16 entradas de 2MB)
    dq 0x200000 + 0x83          ; 0x200000 - 0x3FFFFF (2MB) - Present + RW + 2MB
    dq 0x400000 + 0x83          ; 0x400000 - 0x5FFFFF (2MB) - Present + RW + 2MB
    dq 0x600000 + 0x83          ; 0x600000 - 0x7FFFFF (2MB) - Present + RW + 2MB
    dq 0x800000 + 0x83          ; 0x800000 - 0x9FFFFF (2MB) - Present + RW + 2MB
    dq 0xA00000 + 0x83          ; 0xA00000 - 0xBFFFFF (2MB) - Present + RW + 2MB
    dq 0xC00000 + 0x83          ; 0xC00000 - 0xDFFFFF (2MB) - Present + RW + 2MB
    dq 0xE00000 + 0x83          ; 0xE00000 - 0xFFFFFF (2MB) - Present + RW + 2MB
    dq 0x1000000 + 0x83         ; 0x1000000 - 0x11FFFFF (2MB) - Present + RW + 2MB
    dq 0x1200000 + 0x83         ; 0x1200000 - 0x13FFFFF (2MB) - Present + RW + 2MB
    dq 0x1400000 + 0x83         ; 0x1400000 - 0x15FFFFF (2MB) - Present + RW + 2MB
    dq 0x1600000 + 0x83         ; 0x1600000 - 0x17FFFFF (2MB) - Present + RW + 2MB
    dq 0x1800000 + 0x83         ; 0x1800000 - 0x19FFFFF (2MB) - Present + RW + 2MB
    dq 0x1A00000 + 0x83         ; 0x1A00000 - 0x1BFFFFF (2MB) - Present + RW + 2MB
    dq 0x1C00000 + 0x83         ; 0x1C00000 - 0x1DFFFFF (2MB) - Present + RW + 2MB
    dq 0x1E00000 + 0x83         ; 0x1E00000 - 0x1FFFFFF (2MB) - Present + RW + 2MB
    times 495 dq 0              ; Resto de entradas = 0

; === GLOBAL DESCRIPTOR TABLE ===
align 16
gdt_start:
    ; Descriptor nulo (obligatorio)
    dq 0x0000000000000000

gdt_code:
    ; Descriptor de código 64-bit
    dw 0x0000           ; Limit 15:0 (ignorado en 64-bit)
    dw 0x0000           ; Base 15:0 (ignorado en 64-bit)
    db 0x00             ; Base 23:16 (ignorado en 64-bit)
    db 0x9A             ; Present, DPL=0, Code, Execute/Read
    db 0x20             ; 64-bit mode, no otros flags
    db 0x00             ; Base 31:24 (ignorado en 64-bit)

gdt_data:
    ; Descriptor de datos 64-bit
    dw 0x0000           ; Limit 15:0 (ignorado en 64-bit)
    dw 0x0000           ; Base 15:0 (ignorado en 64-bit)
    db 0x00             ; Base 23:16 (ignorado en 64-bit)
    db 0x92             ; Present, DPL=0, Data, Read/Write
    db 0x00             ; Sin flags especiales
    db 0x00             ; Base 31:24 (ignorado en 64-bit)

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1  ; Tamaño de la GDT
    dd gdt_start                ; Dirección de la GDT

; Selectores
CODE_SEL equ gdt_code - gdt_start   ; 0x08
DATA_SEL equ gdt_data - gdt_start   ; 0x10