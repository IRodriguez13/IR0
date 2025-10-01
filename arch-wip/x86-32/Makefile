# arch/x86-32/Makefile - CORREGIDO

# Configuración específica para x86-32
ARCH_NAME = x86-32

# Usar variables pasadas desde el Makefile principal
CC ?= gcc
ASM ?= nasm
CFLAGS ?= -m32 -march=i686 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -fno-pic -nodefaultlibs -ffreestanding
ASMFLAGS ?= -f elf32

# Fuentes específicas de esta arquitectura
ARCH_SOURCES = sources/arch_x86.c sources/idt_arch_x86.c
BOOT_SOURCES = asm/boot_x86.asm

# Objetos generados
ARCH_OBJECTS = $(ARCH_SOURCES:.c=.o)
BOOT_OBJECTS = $(BOOT_SOURCES:.asm=.o)
ALL_OBJECTS = $(ARCH_OBJECTS) $(BOOT_OBJECTS)

.PHONY: all clean

all: $(ALL_OBJECTS)
	@echo "\033[1;32m============================================================\033[0m"
	@echo "\033[1;32m Boot subsystem compilado correctamente para $(ARCH_NAME)\033[0m"
	@echo "\033[1;32m============================================================\033[0m"

# Reglas de compilación
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	$(ASM) $(ASMFLAGS) $< -o $@

clean:
	rm -f $(ALL_OBJECTS)
	@echo "Limpieza de $(ARCH_NAME) completada"