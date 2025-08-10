# kernelMain 

# drivers/timer/Makefile

PROJECT_ROOT := ../..
CFLAGS := -m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -ffreestanding -Wall -Wextra -O1 -I../includes -I../includes/ir0
ASM = nasm
ASMFLAGS = -f elf32
OBJS = kernel.o # acá en el merge vienen los kmain segun las arch

kernelMain: $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)
