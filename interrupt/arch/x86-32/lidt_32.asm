; idt_load32.s
section .text
    
[bits 32]
global idt_load32_asm
idt_load32_asm:
    mov eax, [esp+4]  ; recibir puntero a idt_ptr desde C
    lidt [eax]         ; cargar IDT
    ret
