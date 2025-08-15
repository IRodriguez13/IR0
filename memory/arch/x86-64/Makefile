# memory/arch/x86-64/Makefile - Paginación y MMU para x86-64

# Usar variables pasadas desde el Makefile principal
CC ?= gcc
CFLAGS ?= -m64 -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -ffreestanding

# Configuración específica para x86-64
ARCH_NAME = x86-64

# Fuentes específicas de x86-64
SOURCES = Paging_x64.c mmu_x64.c
OBJECTS = $(SOURCES:.c=.o)

.PHONY: all clean

all: $(OBJECTS)
	@echo "\033[1;32m============================================================\033[0m"
	@echo "\033[1;32m MM subsystem compilado correctamente para $(ARCH_NAME)\033[0m"
	@echo "\033[1;32m============================================================\033[0m"

%.o: %.c
	@echo "Compilando $< (x86-64)..."
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(OBJECTS:.o=.d)
	@echo "Limpieza x86-64 memoria completada"

# Mostrar configuración
debug:
	@echo "=== X86-64 MEMORY DEBUG ==="
	@echo "SOURCES: $(SOURCES)"
	@echo "OBJECTS: $(OBJECTS)"
	@echo "CFLAGS: $(CFLAGS)"

-include $(OBJECTS:.o=.d)