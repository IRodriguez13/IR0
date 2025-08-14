# Makefile para x64.

# Configuración específica para x64
ARCH_NAME = x64
ARCH_CFLAGS = -m64 -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2
ARCH_LDFLAGS = -m elf_x86_64
ARCH_ASFLAGS = -f elf64

# Fuentes específicas de esta arquitectura
ARCH_SOURCES = sources/arch_x64.c sources/fault.c sources/idt_arch_x64.c
BOOT_SOURCES = asm/boot_x64.asm

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
	@echo "\033[1;32m============================================================\033[0m"
	@echo "\033[1;32m Boot subsystem compilado correctamente para $(ARCH_NAME)\033[0m"
	@echo "\033[1;32m============================================================\033[0m"

clean:
	rm -f $(ALL_OBJECTS)
	@echo "Limpieza de $(ARCH_NAME) completada"

.PHONY: all clean