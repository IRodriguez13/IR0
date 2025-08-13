# kernelMain 

# drivers/timer/Makefile

PROJECT_ROOT := ../..
CFLAGS := -m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -ffreestanding -Wall -Wextra -O1 -I../includes -I../includes/ir0
ASM = nasm
ASMFLAGS = -f elf32
OBJS = kernel_start.o # acá en el merge vienen los kmain segun las arch

kernelMain: $(OBJS)
	@echo "\033[1;32m============================================================\033[0m"
	@echo "\033[1;32m KernelMain subsystem compilado correctamente para $(ARCH_NAME)\033[0m"
	@echo "\033[1;32m============================================================\033[0m"
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)
