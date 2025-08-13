# boot/Makefile x86 (32 bit)

# arch/x86-32/Makefile - CORREGIDO

# Configuración específica para x86-32
ARCH_NAME = x86-32
CC ?= gcc
ASM ?= nasm

# Fuentes específicas de esta arquitectura
ARCH_SOURCES = sources/arch_x86.c sources/idt_arch_x86.c
BOOT_SOURCES = asm/boot_x86.asm asm/interrupt.asm

# Objetos generados
ARCH_OBJECTS = $(ARCH_SOURCES:.c=.o)
BOOT_OBJECTS = $(BOOT_SOURCES:.asm=.o)
ALL_OBJECTS = $(ARCH_OBJECTS) $(BOOT_OBJECTS)

# Rutas específicas para objetos finales (lo que espera el Makefile principal)
FINAL_OBJECTS = arch.o boot.o

.PHONY: all clean

all: $(FINAL_OBJECTS)
	@echo "Compilación de $(ARCH_NAME) completada"

# Crear objetos consolidados que espera el Makefile principal
arch.o: $(ARCH_OBJECTS)
	ld -r $(ARCH_OBJECTS) -o arch.o

boot.o: $(BOOT_OBJECTS)
	ld -r $(BOOT_OBJECTS) -o boot.o

# Reglas de compilación
sources/%.o: sources/%.c
	$(CC) $(CFLAGS) -c $< -o $@

asm/%.o: asm/%.asm
	$(ASM) $(ASMFLAGS) $< -o $@

clean:
	rm -f $(ALL_OBJECTS) $(FINAL_OBJECTS)
	@echo "Limpieza de $(ARCH_NAME) completada"