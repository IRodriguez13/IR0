# boot/Makefile
# Usar flags específicos para kernel
# boot/Makefile
ASM = nasm
ASMFLAGS = -f elf32

.PHONY: all clean

all: boot.o

boot.o: boot.asm
	$(ASM) $(ASMFLAGS) boot.asm -o boot.o

clean:
	rm -f *.o