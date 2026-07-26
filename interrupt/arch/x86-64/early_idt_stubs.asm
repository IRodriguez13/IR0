; SPDX-License-Identifier: GPL-3.0-only
;
; Minimal early exception stubs (x86-64). Record vector/CR2 via C and halt.
; Replaced by the full IDT in idt_init64() / irq_init().

BITS 64

extern early_exception_halt

section .text

%macro EARLY_NOERR 1
global early_isr%1
early_isr%1:
	cli
	push qword 0
	push qword %1
	jmp early_isr_common
%endmacro

%macro EARLY_ERR 1
global early_isr%1
early_isr%1:
	cli
	push qword %1
	jmp early_isr_common
%endmacro

EARLY_NOERR 0
EARLY_NOERR 1
EARLY_NOERR 2
EARLY_NOERR 3
EARLY_NOERR 4
EARLY_NOERR 5
EARLY_NOERR 6
EARLY_NOERR 7
EARLY_ERR 8
EARLY_NOERR 9
EARLY_ERR 10
EARLY_ERR 11
EARLY_ERR 12
EARLY_ERR 13
EARLY_ERR 14
EARLY_NOERR 15
EARLY_NOERR 16
EARLY_ERR 17
EARLY_NOERR 18
EARLY_NOERR 19
EARLY_NOERR 20
EARLY_NOERR 21
EARLY_NOERR 22
EARLY_NOERR 23
EARLY_NOERR 24
EARLY_NOERR 25
EARLY_NOERR 26
EARLY_NOERR 27
EARLY_NOERR 28
EARLY_NOERR 29
EARLY_NOERR 30
EARLY_NOERR 31

early_isr_common:
	; Stack: vec, err, rip, cs, rflags, rsp, ss
	mov rdi, [rsp]		; vector
	mov rsi, [rsp + 8]	; error code
	mov rdx, [rsp + 16]	; rip
	mov rcx, cr2		; fault address
	call early_exception_halt
.hang:
	cli
	hlt
	jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
