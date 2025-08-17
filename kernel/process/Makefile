# kernel/process/Makefile - Compilación del sistema de procesos

# Variables
PROCESS_OBJS = process.o
PROCESS_TARGET = process.a

# Reglas
all: $(PROCESS_TARGET)

$(PROCESS_TARGET): $(PROCESS_OBJS)
	$(AR) rcs $@ $^

process.o: process.c process.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o *.a

.PHONY: all clean
