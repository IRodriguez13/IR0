# Makefile para x64.

# Configuración específica para x64
ARCH_NAME = x64
ARCH_CFLAGS = -m64 -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2
ARCH_LDFLAGS = -m elf_x86_64
ARCH_ASFLAGS = -f elf64

# Fuentes específicas de esta arquitectura
ARCH_SOURCES = arch.c fault.c idt_arch_x64.c
BOOT_SOURCES = boot_x64.asm

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