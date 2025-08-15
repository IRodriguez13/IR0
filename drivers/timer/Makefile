# Makefile para el subsistema de timer

# Usar variables pasadas desde el Makefile principal
CC ?= gcc
ASM ?= nasm
CFLAGS ?= -m32 -march=i686 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -fno-pic -nodefaultlibs -ffreestanding
ASMFLAGS ?= -f elf32

# Detectar arquitectura desde CFLAGS
ifneq (,$(findstring -m64,$(CFLAGS)))
    ARCH_DETECTED = x86-64
    ASMFLAGS = -f elf64
else
    ARCH_DETECTED = x86-32
    ASMFLAGS = -f elf32
endif

# Incluir directorios
INCLUDES = -I../../includes -I../../includes/ir0 -I../../arch/common

# Objetos
OBJS = pit/pit.o lapic/lapic.o hpet/hpet.o hpet/find_hpet.o clock_system.o best_clock.o

all: $(OBJS)
	@echo "\033[1;32m============================================================\033[0m"
	@echo "\033[1;32m Timer compilado correctamente para $(ARCH_DETECTED)\033[0m"
	@echo "\033[1;32m============================================================\033[0m"

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

%.o: %.asm
	$(ASM) $(ASMFLAGS) $< -o $@

clean:
	rm -f $(OBJS)

.PHONY: all clean



