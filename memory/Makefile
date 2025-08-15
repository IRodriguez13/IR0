# Makefile para el subsistema de memoria
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
CFLAGS += -I../includes -I../includes/ir0 -I../arch/common

# Objetos comunes
OBJS = heap_allocator.o physical_allocator.o ondemand-paging.o vallocator.o

# Objetos específicos de arquitectura
ifeq ($(ARCH_DETECTED),x86-64)
    OBJS += arch/x86-64/Paging_x64.o arch/x86-64/mmu_x64.o
else
    OBJS += arch/x_86-32/Paging_x86-32.o arch/x_86-32/mmu_x86-32.o
endif

all: $(OBJS)
	@echo "\033[1;32m===========================================================\033[0m"
	@echo "\033[1;32m Subsistema de memoria compilado exitosamente para $(ARCH_DETECTED)\033[0m"
	@echo "\033[1;32m===========================================================\033[0m"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	$(ASM) $(ASMFLAGS) $< -o $@

clean:
	rm -f $(OBJS)
	@echo "Limpieza de memoria completada"

.PHONY: all clean
