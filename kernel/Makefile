# kernel/Makefile - Compilación del kernel

# Variables
KERNEL_OBJS = kernel_start.o process/process.o scheduler/scheduler.o scheduler/cfs_scheduler.o scheduler/round-robin_scheduler.o scheduler/priority_scheduler.o scheduler/task_impl.o syscalls/syscalls.o shell/shell.o elf_loader.o
KERNEL_TARGET = kernel.a

# Reglas
all: $(KERNEL_TARGET)

$(KERNEL_TARGET): $(KERNEL_OBJS)
	$(AR) rcs $@ $^

kernel_start.o: kernel_start.c kernel_start.h
	$(CC) $(CFLAGS) -c $< -o $@

elf_loader.o: elf_loader.c elf_loader.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o *.a

.PHONY: all clean
