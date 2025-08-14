# interrupt/Makefile

CC := gcc
CFLAGS := -m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -ffreestanding -Wall -Wextra -O1 -I../includes -I../includes/ir0
ASM ?= nasm

# Detectar arquitectura
ifeq ($(ARCH),x86-64)
    ARCH_NAME = x86-64
    ASMFLAGS = -f elf64
    ARCH_OBJ = arch/x86-64/interrupt.o
else
    ARCH_NAME = x86-32
    ASMFLAGS = -f elf32
    ARCH_OBJ = arch/x86-32/interrupt.o
endif

# Todos los objetos que necesitas en esta carpeta
OBJS = idt.o isr_handlers.o $(ARCH_OBJ)

.PHONY: all clean

# Target principal
all: $(OBJS)
	@echo "\033[1;32m============================================================\033[0m"
	@echo "\033[1;32m interrupt subsystem compilado correctamente para $(ARCH_NAME)\033[0m"
	@echo "\033[1;32m============================================================\033[0m"

# Reglas para compilar archivos .c
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	nasm $(ASMFLAGS) $< -o $@

# Regla específica para x86-64
arch/x86-64/interrupt.o: arch/x86-64/interrupt.asm
	nasm $(ASMFLAGS) $< -o $@

# Regla específica para x86-32
arch/x86-32/interrupt.o: arch/x86-32/interrupt.asm
	nasm $(ASMFLAGS) $< -o $@

idt.o: idt.c
	$(CC) $(CFLAGS) -c idt.c -o idt.o

isr_handlers.o: isr_handlers.c
	$(CC) $(CFLAGS) -c isr_handlers.c -o isr_handlers.o

clean:
	rm -f $(OBJS) arch/x86-32/interrupt.o arch/x86-64/interrupt.o