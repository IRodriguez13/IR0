# memory/Makefile - Sistema de memoria multi-arquitectura 

PROJECT_ROOT := ..
MEMORY_ROOT := $(CURDIR)

# Configuración heredada del Makefile principal
CC ?= gcc
CFLAGS := -m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -ffreestanding
CFLAGS += -Wall -Wextra -O1 -MMD -MP
CFLAGS += -I$(PROJECT_ROOT)/includes -I$(PROJECT_ROOT)/includes/ir0 
CFLAGS += -I$(PROJECT_ROOT)/arch/common

# Detectar arquitectura para seleccionar implementación correcta
ARCH ?= x86-32

ifeq ($(ARCH),x_64)
    ARCH_MEMORY_DIR = arch/x_64
    CFLAGS += -m64 -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2
    CFLAGS += -I$(PROJECT_ROOT)/arch/x_64/include
else ifeq ($(ARCH),x86-32)
    ARCH_MEMORY_DIR = arch/x_86-32
    CFLAGS += -m32 -march=i686
    CFLAGS += -I$(PROJECT_ROOT)/arch/x86-32/include
else
    $(error Arquitectura no soportada: $(ARCH))
endif

# Archivos comunes (independientes de arquitectura)
COMMON_SOURCES = heap_allocator.c physical_allocator.c
COMMON_OBJECTS = $(COMMON_SOURCES:.c=.o)

# Archivos específicos de arquitectura
ARCH_SOURCES = $(ARCH_MEMORY_DIR)/mmu_$(ARCH).c $(ARCH_MEMORY_DIR)/Paging_$(ARCH).c
ARCH_OBJECTS = $(ARCH_SOURCES:.c=.o)

# Todos los objetos
ALL_OBJECTS = $(COMMON_OBJECTS) $(ARCH_OBJECTS)

.PHONY: all clean arch-objects common-objects

all: common-objects arch-objects
	@echo "Subsistema de memoria compilado para $(ARCH)"

common-objects: $(COMMON_OBJECTS)

arch-objects: $(ARCH_OBJECTS)

# Reglas de compilación
%.o: %.c
	@echo "Compilando $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Compilación específica para archivos de arquitectura
$(ARCH_MEMORY_DIR)/%.o: $(ARCH_MEMORY_DIR)/%.c
	@echo "Compilando específico de arquitectura: $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Limpieza
clean:
	@echo "Limpiando subsistema de memoria..."
	rm -f $(ALL_OBJECTS) $(ALL_OBJECTS:.o=.d)
	@for dir in arch/*/; do \
		if [ -d "$$dir" ]; then \
			$(MAKE) -C $$dir clean; \
		fi \
	done

# Debug: mostrar configuración
debug:
	@echo "=== MEMORY SUBSYSTEM DEBUG ==="
	@echo "ARCH: $(ARCH)"
	@echo "ARCH_MEMORY_DIR: $(ARCH_MEMORY_DIR)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "COMMON_OBJECTS: $(COMMON_OBJECTS)"
	@echo "ARCH_OBJECTS: $(ARCH_OBJECTS)"
	@echo "ALL_OBJECTS: $(ALL_OBJECTS)"

-include $(ALL_OBJECTS:.o=.d)