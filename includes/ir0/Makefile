# Makefile para includes/ir0

# Usar variables pasadas desde el Makefile principal (no fijar -m32 aquí)
CC ?= gcc
CFLAGS ?= -Wall -Wextra -O1 -MMD -MP

# Detectar arquitectura desde CFLAGS
ifneq (,$(findstring -m64,$(CFLAGS)))
    ARCH_DETECTED = x86-64
else
    ARCH_DETECTED = x86-32
endif

# Incluir directorios
CFLAGS += -I../.. -I.

# Objetos
OBJS = print.o logging.o validation.o oops.o

all: $(OBJS)
	@echo "\033[1;32m============================================================\033[0m"
	@echo "\033[1;32m IR0 includes compilado correctamente para $(ARCH_DETECTED)\033[0m"
	@echo "\033[1;32m============================================================\033[0m"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)
	rm -f *.d

.PHONY: all clean

-include $(OBJS:.o=.d)
