# boot/Makefile x86 (32 bit)

# Configuración específica para x86-64 (32-bit)
ARCH_NAME = x86-64
ARCH_CFLAGS = -m32 -march=i686 -fno-pie -fno-stack-protector
ARCH_LDFLAGS = -m elf_i386
ARCH_ASFLAGS = -f elf32

# Fuentes específicas de esta arquitectura
ARCH_SOURCES = arch.c idt_arch_x86.c
BOOT_SOURCES = boot_x86.asm

# Objetos generados
ARCH_OBJECTS = $(ARCH_SOURCES:.c=.o)
BOOT_OBJECTS = $(BOOT_SOURCES:.asm=.o)
ALL_OBJECTS = $(ARCH_OBJECTS) $(BOOT_OBJECTS)

# Reglas de compilación
%.o: %.c
	$(CC) $(CFLAGS) $(ARCH_CFLAGS) -c $< -o $@

%.o: %.asm
	nasm $(ARCH_ASFLAGS) $< -o $@

all: $(ALL_OBJECTS)
	@echo "Compilación de $(ARCH_NAME) completada"

clean:
	rm -f $(ALL_OBJECTS)
	@echo "Limpieza de $(ARCH_NAME) completada"

.PHONY: all clean