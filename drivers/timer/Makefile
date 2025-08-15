# Makefile para el subsistema de timer
CC = gcc
ASM = nasm

# Detectar arquitectura desde CFLAGS
ifneq (,$(findstring -m64,$(CFLAGS)))
    ARCH_DETECTED = x86-64
else
    ARCH_DETECTED = x86-32
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
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	$(ASM) $(ASMFLAGS) $< -o $@

clean:
	rm -f $(OBJS)

.PHONY: all clean



