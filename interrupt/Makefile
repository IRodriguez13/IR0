# interrupt/Makefile

CC := gcc
CFLAGS := -m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -ffreestanding -Wall -Wextra -O1 -I../includes -I../includes/ir0
ASM := nasm
ASMFLAGS := -f elf32

# Todos los objetos que necesitas en esta carpeta
OBJS = interrupt.o idt.o isr_handlers.o

.PHONY: all clean

# Target principal
all: $(OBJS)

# Reglas para compilar archivos .c
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Reglas para compilar archivos .asm
%.o: %.asm
	$(ASM) $(ASMFLAGS) $< -o $@

# Reglas específicas (opcional, las reglas genéricas arriba ya las cubren)
interrupt.o: interrupt.asm
	$(ASM) $(ASMFLAGS) interrupt.asm -o interrupt.o

idt.o: idt.c
	$(CC) $(CFLAGS) -c idt.c -o idt.o

isr_handlers.o: isr_handlers.c
	$(CC) $(CFLAGS) -c isr_handlers.c -o isr_handlers.o

clean:
	rm -f $(OBJS)