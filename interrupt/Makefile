# Makefile para el subsistema de interrupciones

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

# Objetos comunes
OBJS = idt.o isr_handlers.o

# Objetos específicos de arquitectura
ifeq ($(ARCH_DETECTED),x86-64)
    OBJS += arch/x86-64/interrupt.o
else
    OBJS += arch/x86-32/interrupt.o
endif

all: $(OBJS)
	@echo "\033[1;32m============================================================\033[0m"
	@echo "\033[1;32m interrupt subsystem compilado correctamente para $(ARCH_DETECTED)\033[0m"
	@echo "\033[1;32m============================================================\033[0m"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	$(ASM) $(ASMFLAGS) $< -o $@

clean:
	rm -f $(OBJS)

.PHONY: all clean