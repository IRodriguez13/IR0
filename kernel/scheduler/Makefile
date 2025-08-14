# kernel/scheduler/Makefile 

CC := gcc
ASM := nasm

# ===============================================================================
# CONFIGURACIÓN MULTI-ARQUITECTURA
# ===============================================================================

# Detectar arquitectura y configurar flags apropiados
ifeq ($(ARCH),x86-64)
    # Configuración para 64-bit
    CFLAGS := -m64 -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2
    CFLAGS += -nostdlib -nostdinc -fno-builtin -fno-stack-protector -ffreestanding
    CFLAGS += -Wall -Wextra -O1 -I../../includes -I../../includes/ir0
    ASMFLAGS := -f elf64
    SWITCH_OBJ = switch/switch_x64.o
    ARCH_NAME = x86-64
else
    # Configuración para 32-bit (default)
    CFLAGS := -m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -ffreestanding
    CFLAGS += -Wall -Wextra -O1 -I../../includes -I../../includes/ir0
    ASMFLAGS := -f elf32
    SWITCH_OBJ = switch/switch_x86.o
    ARCH_NAME = x86-32
endif

# ===============================================================================
# OBJETOS A COMPILAR
# ===============================================================================

SCHEDULER_OBJS = priority_scheduler.o round-robin_scheduler.o sched_central.o \
                 scheduler_detection.o cfs_scheduler.o

ALL_OBJS = $(SCHEDULER_OBJS) $(SWITCH_OBJ)

# ===============================================================================
# TARGETS PRINCIPALES
# ===============================================================================

.PHONY: all clean debug

all: $(ALL_OBJS)
	@echo "\033[1;32m============================================================\033[0m"
	@echo "\033[1;32m Scheduler subsystem compilado correctamente para $(ARCH_NAME)\033[0m"
	@echo "\033[1;32m============================================================\033[0m"

# ===============================================================================
# REGLAS DE COMPILACIÓN ESPECÍFICAS POR ARQUITECTURA
# ===============================================================================

# Switch context para x86-64
switch/switch_x64.o: switch/switch_x64.asm
	@echo "Compilando context switch para x86-64..."
	$(ASM) $(ASMFLAGS) $< -o $@

# Switch context para x86-32
switch/switch_x86.o: switch/switch_x86.asm
	@echo "Compilando context switch para x86-32..."
	$(ASM) $(ASMFLAGS) $< -o $@

# ===============================================================================
# REGLAS GENERALES
# ===============================================================================

# Compilar archivos .c
%.o: %.c
	@echo "Compilando $< para $(ARCH_NAME)..."
	$(CC) $(CFLAGS) -c $< -o $@

# Compilar archivos .asm generales
%.o: %.asm
	@echo "Compilando ASM $< para $(ARCH_NAME)..."
	$(ASM) $(ASMFLAGS) $< -o $@

# ===============================================================================
# LIMPIEZA
# ===============================================================================

clean:
	@echo "Limpiando scheduler subsystem..."
	rm -f $(ALL_OBJS)
	@echo "Limpiando archivos de dependencias (.d)..."
	rm -f *.d switch/*.d
	@echo "Limpieza del scheduler completada."

# ===============================================================================
# DEBUG Y UTILIDADES
# ===============================================================================

debug:
	@echo "=== SCHEDULER MAKEFILE DEBUG ==="
	@echo "ARCH: $(ARCH)"
	@echo "ARCH_NAME: $(ARCH_NAME)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "ASMFLAGS: $(ASMFLAGS)"
	@echo "SWITCH_OBJ: $(SWITCH_OBJ)"
	@echo "ALL_OBJS: $(ALL_OBJS)"

# Verificar que los archivos fuente existen
check-sources:
	@echo "Verificando archivos fuente..."
	@for obj in $(SCHEDULER_OBJS); do \
		src=$${obj%.o}.c; \
		if [ ! -f "$$src" ]; then \
			echo "❌ FALTA: $$src"; \
		else \
			echo "✅ $$src"; \
		fi; \
	done
	@if [ -f "switch/switch_x86.asm" ]; then \
		echo "✅ switch/switch_x86.asm"; \
	else \
		echo "❌ FALTA: switch/switch_x86.asm"; \
	fi
	@if [ -f "switch/switch_x64.asm" ]; then \
		echo "✅ switch/switch_x64.asm"; \
	else \
		echo "❌ FALTA: switch/switch_x64.asm"; \
	fi

# ===============================================================================
# DEPENDENCIAS (auto-generadas)
# ===============================================================================

-include $(SCHEDULER_OBJS:.o=.d)