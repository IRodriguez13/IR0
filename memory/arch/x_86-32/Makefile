# memory/arch/x_86-32/Makefile - Paginación y MMU para x86-32

# Usar variables pasadas desde el Makefile principal
CC ?= gcc
CFLAGS ?= -m32 -march=i686 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -ffreestanding

# Configuración específica para x86-32
ARCH_NAME = x86-32

SOURCES = Paging_x86-32.c mmu_x86-32.c
OBJECTS = $(SOURCES:.c=.o)

all: $(OBJECTS)
	@echo "\033[1;32m============================================================\033[0m"
	@echo "\033[1;32m MM subsystem compilado correctamente para $(ARCH_NAME)\033[0m"
	@echo "\033[1;32m============================================================\033[0m"

%.o: %.c
	@echo "Compilando $< (x86-32)..."
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(OBJECTS:.o=.d)
	@echo "Limpieza x86-32 memoria completada"

-include $(OBJECTS:.o=.d)
