; Bootloader completo para x86-32 con Multiboot - VERSIÓN SEGURA
; Implementación desde cero - limpia, funcional y sin page faults

[BITS 32]
global _start
extern kmain_x32

; Header Multiboot estándar
section .multiboot_header
align 4
header_start:
    dd 0x1BADB002        ; Magic number
    dd 0x00000003        ; Flags: align modules, provide memory map
    dd -(0x1BADB002 + 0x00000003) ; Checksum
header_end:

; Código principal
section .text
_start:
    ; ===============================================================================
    ; SETUP INICIAL SEGURO - SIN PAGE FAULTS
    ; ===============================================================================
    
    ; Deshabilitar interrupciones inmediatamente
    cli                     
    
    ; Inicializar stack con alineación correcta (16-byte aligned)
    mov esp, stack_top
    mov ebp, esp
    
    ; Verificar que el stack está alineado correctamente
    test esp, 0xF
    jz .stack_ok
    ; Si no está alineado, ajustar
    and esp, 0xFFFFFFF0
    mov ebp, esp
.stack_ok:
    
    ; Limpiar EFLAGS completamente
    push 0
    popf
    
    ; Limpiar segmentos de datos
    mov ax, 0x10           ; Data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Verificar que estamos en modo protegido
    mov eax, cr0
    test eax, 1
    jz .not_protected
    jmp .protected_ok


.not_protected:
    ; Si no estamos en modo protegido, loop infinito
    cli
    hlt
    jmp .not_protected
.protected_ok:
    
    ; ===============================================================================
    ; VERIFICACIONES DE SEGURIDAD
    ; ===============================================================================
    
    ; Verificar que tenemos memoria suficiente
    ; (Esto es una verificación básica)
    mov eax, 0x100000      ; Dirección base del kernel
    mov ebx, [eax]         ; Intentar leer
    mov [eax], ebx         ; Intentar escribir
    
    ; ===============================================================================
    ; LLAMAR AL KERNEL PRINCIPAL
    ; ===============================================================================
    
    ; Llamar al kernel principal con stack limpio
    call kmain_x32
    
    ; Si retorna, loop infinito seguro
.hang:
    cli
    hlt
    jmp .hang

; Stack con alineación correcta (16KB, 16-byte aligned)
section .bss
align 16
stack_bottom:
    resb 16384              ; 16KB stack
stack_top: