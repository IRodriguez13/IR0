; boot.asm - Punto de entrada Multiboot2 completo para 64-bit
bits 32                         ; GRUB inicia en modo 32-bit

section .multiboot_header
header_start:
    ; Encabezado Multiboot2
    dd 0xe85250d6               ; Número mágico Multiboot2
    dd 0                        ; Arquitectura 0 (i386 protegido)
    dd header_end - header_start ; Longitud del encabezado
    ; Checksum
    dd 0x100000000 - (0xe85250d6 + 0 + (header_end - header_start))

    ; Tags requeridos
    ; Tag de información de memoria (opcional)
    dw 2                        ; Type: information request
    dw 0                        ; Flags
    dd 24                       ; Size
    dd 4                        ; Memory map tag
    dd 6                        ; Framebuffer tag
    dd 0                        ; Terminator

    ; Tag de terminación
    dw 0                        ; Type
    dw 0                        ; Flags
    dd 8                        ; Size
header_end:

section .text
global _start
extern kmain_x64              ; Definido en arch.c

_start:
    ; Configurar stack
    mov esp, stack_top

    ; Verificar que GRUB nos dejó en modo 32-bit
    call check_multiboot
    call check_cpuid
    call check_long_mode

    ; Configurar paginación (temporal para el salto a 64-bit)
    call setup_page_tables
    call enable_paging

    ; Cargar GDT 64-bit
    lgdt [gdt64.pointer]
    
    ; Saltar a modo 64-bit
    jmp gdt64.code:long_mode_start

bits 64
long_mode_start:
    ; Limpiar segmentos de datos
    mov ax, gdt64.data
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Cargo el kernel
    call kmain_x64

    ; En caso de retorno, fallback donde duerme la cpu
    cli
.halt_fallback:
    hlt
    jmp .halt_fallback

; ===============================================================================
; Funciones de verificación y configuración (32-bit)
; ===============================================================================
bits 32

check_multiboot:
    cmp eax, 0x36d76289        ; Magic number de Multiboot2
    jne .no_multiboot
    ret
.no_multiboot:
    mov al, "0"
    jmp error

check_cpuid:
    ; Verificar si CPUID está disponible intentando cambiar flag ID
    pushfd                     ; Guardar EFLAGS
    pop eax                    ; EFLAGS → EAX
    mov ecx, eax              ; Backup de EFLAGS
    xor eax, 1 << 21          ; Toggle bit ID (bit 21)
    push eax                  ; EAX → stack
    popfd                     ; stack → EFLAGS
    
    ; Leer EFLAGS nuevamente
    pushfd
    pop eax
    
    ; Restaurar EFLAGS original
    push ecx
    popfd
    
    ; Comparar: si no cambió, CPUID no disponible
    xor eax, ecx
    jz .no_cpuid              ; Si es 0, no cambió = no CPUID
    ret

.no_cpuid:
    mov al, "1"
    jmp error

check_long_mode:
    ; Primero verificar si extended CPUID está disponible
    mov eax, 0x80000000       ; Extended CPUID
    cpuid
    cmp eax, 0x80000001       ; Verificar si soporta 0x80000001
    jb .no_long_mode          ; Si no, no hay long mode
    
    ; Verificar long mode bit
    mov eax, 0x80000001       ; Extended feature info
    cpuid
    test edx, 1 << 29         ; Long mode bit (bit 29)
    jz .no_long_mode          ; Si no está, no long mode
    ret

.no_long_mode:
    mov al, "2"
    jmp error

setup_page_tables:
    ; Limpiar las tablas de paginación
    mov edi, 0x1000           ; Dirección base para page tables
    mov cr3, edi              ; CR3 = 0x1000 (Page Map Level 4)
    xor eax, eax              ; EAX = 0
    mov ecx, 4096             ; Limpiar 4096 bytes (1 página)
    rep stosd                 ; Llenar con 0s
    mov edi, 0x1000           ; Resetear EDI

    ; Configurar Page Map Level 4 (PML4)
    mov dword [edi], 0x2003   ; PML4[0] → 0x2000 (present, writable)
    add edi, 0x1000           ; EDI = 0x2000 (Page Directory Pointer Table)
    
    ; Configurar Page Directory Pointer Table (PDPT)  
    mov dword [edi], 0x3003   ; PDPT[0] → 0x3000 (present, writable)
    add edi, 0x1000           ; EDI = 0x3000 (Page Directory)
    
    ; Configurar Page Directory (2MB pages)
    mov dword [edi], 0x83     ; PD[0] = 0x0000000 (present, writable, huge page)
    add edi, 8
    mov dword [edi], 0x200083 ; PD[1] = 0x0200000 (present, writable, huge page)
    add edi, 8
    mov dword [edi], 0x400083 ; PD[2] = 0x0400000 (present, writable, huge page)
    add edi, 8
    mov dword [edi], 0x600083 ; PD[3] = 0x0600000 (present, writable, huge page)
    
    ret

enable_paging:
    ; Habilitar PAE (Physical Address Extension)
    mov eax, cr4
    or eax, 1 << 5            ; PAE bit (bit 5)
    mov cr4, eax
    
    ; Habilitar long mode en EFER MSR
    mov ecx, 0xC0000080       ; EFER MSR
    rdmsr                     ; Leer MSR
    or eax, 1 << 8            ; Long Mode Enable (bit 8)
    wrmsr                     ; Escribir MSR
    
    ; Habilitar paging
    mov eax, cr0
    or eax, 1 << 31           ; Paging Enable (bit 31)
    mov cr0, eax              ; Activar paginación + long mode
    
    ret

error:
    ; Manejo básico de errores - mostrar código en pantalla
    mov dword [0xb8000], 0x4f524f45  ; "ER" en rojo
    mov dword [0xb8004], 0x4f3a4f52  ; "R:" en rojo
    mov dword [0xb8008], 0x4f204f20  ; "  " en rojo
    mov byte  [0xb800a], al          ; Código de error
    cli
    hlt

; ===============================================================================
; Datos y estructuras
; ===============================================================================

section .bss
align 16
stack_bottom:
    resb 16384                  ; 16KB de stack
stack_top:

section .rodata
gdt64:
    dq 0                        ; Entrada nula
.code: equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53) ; Código 64-bit
.data: equ $ - gdt64  
    dq (1<<44) | (1<<47)        ; Datos 64-bit
.pointer:
    dw $ - gdt64 - 1           ; Límite
    dq gdt64                   ; Base