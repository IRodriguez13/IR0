; --------------------------------------------
; Kernel Entry Point for Multiboot2 Loaders   |
; --------------------------------------------|
; Expects:                                    |
;   - EAX = 0x36d76289 (Multiboot2 magic)     |
;   - EBX = Pointer to Multiboot2 info struct |
; Guarantees:                                 |
;   - Stack: 16KB aligned to 16 bytes         |
;   - Interrupts: Disabled                    |
;   - EFLAGS: Clean state                     |
; --------------------------------------------

; boot.asm - Punto de entrada para kernel Multiboot2
section .multiboot_header
header_start:
    dd 0xe85250d6                ; Magic number (Multiboot 2)
    dd 0                         ; Architecture 0 (i386)
    dd header_end - header_start ; Header length
    dd 0x100000000 - (0xe85250d6 + 0 + (header_end - header_start)) ; Checksum
    
    ; Tags opcionales (ej: framebuffer request)
    ; dw 5, 0, 20, 1024, 768, 32  ; Tag framebuffer
    dw 0, 0, 8                   ; End tag
header_end:

[BITS 32]
section .text
global _start
extern kmain_x32

_start:
    cli
    
    ; Inicializar stack (16KB alineado)
    mov esp, stack_top
    mov ebp, esp
    
    ; Limpiar EFLAGS
    push 0
    popf
    
    ; Pasar parámetros Multiboot a kernel_main
    push ebx    ; Puntero a estructura Multiboot
    push eax    ; Magic number (0x36d76289)
    
    call kmain_x32
    
    ; Caída segura si kmain_x32 retorna que no debería pasar.


; hang hace lo mismo que panic en el fondo pero mas rústico. Cuelga la cpu pero en un bucle infinito.
.hang:

    cli
    hlt
    jmp .hang

.end:

section .bss

align 16

stack_bottom:
  
    resb 16384  ; 16KB stack

stack_top:    


; Esto es el punto real de entrada del sistema donde permitimos a los módulos de C funcionar, por eso no usamos main() 
;como punto de entrada, ya que no tenemos runtime para matar procesos y definir su ciclo de vida como en los programas típicos de 
;usuario donde prima: loader → _start → main() → return 0 → exit(0) → syscall exit → SO libera recursos
;acá sería algo como GRUB → _start → Kernel_Main() → lo que sea que haga esa funcion. 
; Una vez que se llama a kernel_main, no hay retorno posible.
; No existe un sistema operativo que nos esté esperando, ni una syscall exit()
; como en el espacio de usuario. Somos el único código ejecutándose.
; Si kernel_main termina, ejecutamos un loop con hlt → jmp .hang
; para evitar comportamiento indefinido.
;
; Si queremos terminar el sistema, debemos apagarlo manualmente (out 0x604, al)
; o reiniciarlo (out 0x64, al con 0xFE). Por ahora, simplemente colgamos la CPU.
;