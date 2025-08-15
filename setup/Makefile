# Makefile para el subsistema de configuración

# Usar variables pasadas desde el Makefile principal
CC ?= gcc
CFLAGS ?= -m32 -march=i686 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -fno-pic -nodefaultlibs -ffreestanding

# Detectar arquitectura desde CFLAGS
ifneq (,$(findstring -m64,$(CFLAGS)))
    ARCH_DETECTED = x86-64
else
    ARCH_DETECTED = x86-32
endif

# Incluir directorios
CFLAGS += -I../includes -I../includes/ir0

# Objetos
OBJS = kernel_config.o

all: $(OBJS)
	@echo "\033[1;32m============================================================\033[0m"
	@echo "\033[1;32m Setup subsystem compilado correctamente para $(ARCH_DETECTED)\033[0m"
	@echo "\033[1;32m============================================================\033[0m"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)

.PHONY: all clean
