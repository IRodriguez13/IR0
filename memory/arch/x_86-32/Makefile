# memory/arch/x_86-32/Makefile - Paginación y MMU para x86-32

PROJECT_ROOT := ../../..
MEMORY_ARCH_ROOT := $(CURDIR)

# Configuración específica para x86-32
CC ?= gcc
CFLAGS := -m32 -march=i686 -nostdlib -nostdinc -fno-builtin -fno-stack-protector
CFLAGS += -ffreestanding -Wall -Wextra -O1 -MMD -MP
CFLAGS += -I$(PROJECT_ROOT)/includes -I$(PROJECT_ROOT)/includes/ir0
CFLAGS += -I$(PROJECT_ROOT)/arch/common -I$(PROJECT_ROOT)/arch/x86-32/include

# Fuentes específicas de x86-32  
SOURCES = Paging_x86-32.c mmu_x86-32.c
OBJECTS = $(SOURCES:.c=.o)

.PHONY: all clean

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

# Mostrar configuración
debug:
	@echo "=== X86-32 MEMORY DEBUG ==="
	@echo "SOURCES: $(SOURCES)"
	@echo "OBJECTS: $(OBJECTS)"
	@echo "CFLAGS: $(CFLAGS)"

-include $(OBJECTS:.o=.d)