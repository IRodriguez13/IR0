# interrupt/Makefile

CC := gcc
CFLAGS := -m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -ffreestanding -Wall -Wextra -O1 -I../includes -I../includes/ir0


# Todos los objetos que necesitas en esta carpeta
OBJS =  idt.o isr_handlers.o

.PHONY: all clean

# Target principal
all: $(OBJS)

# Reglas para compilar archivos .c
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

idt.o: idt.c
	$(CC) $(CFLAGS) -c idt.c -o idt.o

isr_handlers.o: isr_handlers.c
	$(CC) $(CFLAGS) -c isr_handlers.c -o isr_handlers.o

clean:
	rm -f $(OBJS)