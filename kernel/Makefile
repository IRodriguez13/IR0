# Makefile para el kernel principal
CC = gcc

# Detectar arquitectura desde CFLAGS
ifneq (,$(findstring -m64,$(CFLAGS)))
    ARCH_DETECTED = x86-64
else
    ARCH_DETECTED = x86-32
endif

# Objetos
OBJS = kernel_start.o

all: $(OBJS)
	@echo "\033[1;32m============================================================\033[0m"
	@echo "\033[1;32m KernelMain subsystem compilado correctamente para $(ARCH_DETECTED)\033[0m"
	@echo "\033[1;32m============================================================\033[0m"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)

.PHONY: all clean
