# kernel/Makefile - Compilación del kernel

# Variables
KERNEL_OBJS = kernel.o init.o process.o scheduler.o task.o syscalls.o dbgshell.o elf_loader.o typewriter.o
KERNEL_TARGET = kernel.a

# Reglas
all: $(KERNEL_TARGET)

$(KERNEL_TARGET): $(KERNEL_OBJS)
	$(AR) rcs $@ $^

kernel.o: kernel.c
	$(CC) $(CFLAGS) -c $< -o $@

init.o: init.c
	$(CC) $(CFLAGS) -c $< -o $@

process.o: process.c process.h
	$(CC) $(CFLAGS) -c $< -o $@

scheduler.o: scheduler.c scheduler.h
	$(CC) $(CFLAGS) -c $< -o $@

task.o: task.c task.h
	$(CC) $(CFLAGS) -c $< -o $@

syscalls.o: syscalls.c syscalls.h
	$(CC) $(CFLAGS) -c $< -o $@

dbgshell.o: dbgshell.c
	$(CC) $(CFLAGS) -c $< -o $@

elf_loader.o: elf_loader.c elf_loader.h
	$(CC) $(CFLAGS) -c $< -o $@

typewriter.o: typewriter.c typewriter.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o *.a

.PHONY: all clean
