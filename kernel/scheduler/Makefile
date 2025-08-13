# scheduler/Makefile

CC := gcc
CFLAGS := -m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -ffreestanding -Wall -Wextra -O1 -I../includes -I../includes/ir0
ASM := nasm
ASMFLAGS := -f elf32

# Objetos en esta carpeta y subcarpetas
OBJS = scheduler.o switch/switch_x86.o

.PHONY: all clean

all: $(OBJS)
	@echo "\033[1;32m============================================================\033[0m"
	@echo "\033[1;32m Sched subsystem compilado correctamente para $(ARCH_NAME)\033[0m"
	@echo "\033[1;32m============================================================\033[0m"

# Reglas para archivos .c
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Reglas para archivos .asm (incluyendo subcarpetas)
%.o: %.asm
	$(ASM) $(ASMFLAGS) $< -o $@

# Regla específica para switch.asm en subcarpeta
switch/switch.o: switch/switch.asm
	$(ASM) $(ASMFLAGS) switch/switch_x86.asm -o switch/switch_x86.o

# Regla específica para scheduler.c
scheduler.o: scheduler.c
	$(CC) $(CFLAGS) -c scheduler.c -o scheduler.o

clean:
	rm -f $(OBJS)