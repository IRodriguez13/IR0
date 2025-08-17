# kernel/syscalls/Makefile - Compilación del sistema de system calls

# Variables
SYSCALLS_OBJS = syscalls.o
SYSCALLS_TARGET = syscalls.a

# Reglas
all: $(SYSCALLS_TARGET)

$(SYSCALLS_TARGET): $(SYSCALLS_OBJS)
	$(AR) rcs $@ $^

syscalls.o: syscalls.c syscalls.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o *.a

.PHONY: all clean
