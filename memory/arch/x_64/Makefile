# memory/arch/x_64/Makefile - Paginación y MMU para x86-64

PROJECT_ROOT := ../../..
MEMORY_ARCH_ROOT := $(CURDIR)

# Configuración específica para x86-64
CC ?= gcc
CFLAGS := -m64 -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2
CFLAGS += -nostdlib -nostdinc -fno-builtin -fno-stack-protector -ffreestanding
CFLAGS += -Wall -Wextra -O1 -MMD -MP
CFLAGS += -I$(PROJECT_ROOT)/includes -I$(PROJECT_ROOT)/includes/ir0
CFLAGS += -I$(PROJECT_ROOT)/arch/common -I$(PROJECT_ROOT)/arch/x_64/include

# Fuentes específicas de x86-64
SOURCES = Paging_x64.c mmu_x64.c
OBJECTS = $(SOURCES:.c=.o)

.PHONY: all clean

all: $(OBJECTS)
	@echo "Memoria x86-64 compilada"

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