# Makefile para el subsistema de scheduler
CC = gcc
ASM = nasm

# Detectar arquitectura desde CFLAGS
ifneq (,$(findstring -m64,$(CFLAGS)))
    ARCH_DETECTED = x86-64
    ASMFLAGS = -f elf64
else
    ARCH_DETECTED = x86-32
    ASMFLAGS = -f elf32
endif

# Incluir directorios
CFLAGS += -I../../includes -I../../includes/ir0 -I../../arch/common

# Objetos comunes
OBJS = priority_scheduler.o round-robin_scheduler.o sched_central.o scheduler_detection.o cfs_scheduler.o task_impl.o

# Objetos específicos de arquitectura
ifeq ($(ARCH_DETECTED),x86-64)
    OBJS += switch/switch_x64.o
else
    OBJS += switch/switch_x86.o
endif

all: $(OBJS)
	@echo "\033[1;32m============================================================\033[0m"
	@echo "\033[1;32m Scheduler subsystem compilado correctamente para $(ARCH_DETECTED)\033[0m"
	@echo "\033[1;32m============================================================\033[0m"

%.o: %.c
	@echo "Compilando $< para $(ARCH_DETECTED)..."
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	@echo "Compilando context switch para $(ARCH_DETECTED)..."
	$(ASM) $(ASMFLAGS) $< -o $@

clean:
	@echo "Limpiando scheduler subsystem..."
	rm -f $(OBJS)
	@echo "Limpiando archivos de dependencias (.d)..."
	rm -f *.d switch/*.d
	@echo "Limpieza del scheduler completada."

.PHONY: all clean