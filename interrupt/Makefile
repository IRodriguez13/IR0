# Makefile para el sistema de interrupciones

# Variables
CC = gcc
NASM = nasm
CFLAGS = -Wall -Wextra -O1 -MMD -MP
INCLUDES = -I../../includes -I../../includes/ir0 -I../../arch/common -I../../setup

# Objetos comunes
COMMON_OBJS = arch/idt.o arch/pic.o arch/isr_handlers.o arch/keyboard.o

# Objetos específicos por arquitectura
X86_32_OBJS = arch/x86-32/isr_stubs_32.o
X86_64_OBJS = arch/x86-64/isr_stubs_64.o

# Objetos finales
INTERRUPT_OBJS = $(COMMON_OBJS)

# Reglas principales
all: $(INTERRUPT_OBJS)

# Compilar archivos C
arch/idt.o: arch/idt.c arch/idt.h arch/io.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

arch/pic.o: arch/pic.c arch/pic.h arch/io.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

arch/isr_handlers.o: arch/isr_handlers.c arch/idt.h arch/pic.h arch/io.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

arch/keyboard.o: arch/keyboard.c arch/idt.h arch/pic.h arch/io.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Compilar stubs de 32-bit
arch/x86-32/isr_stubs_32.o: arch/x86-32/isr_stubs_32.asm
	$(NASM) -f elf32 $< -o $@

# Compilar stubs de 64-bit
arch/x86-64/isr_stubs_64.o: arch/x86-64/isr_stubs_64.asm
	$(NASM) -f elf64 $< -o $@

# Limpiar
clean:
	rm -f $(COMMON_OBJS) $(X86_32_OBJS) $(X86_64_OBJS) *.d

# Información
info:
	@echo "Sistema de Interrupciones - Objetos:"
	@echo "  Comunes: $(COMMON_OBJS)"
	@echo "  32-bit:  $(X86_32_OBJS)"
	@echo "  64-bit:  $(X86_64_OBJS)"

.PHONY: all clean info