# Panic makefile

CC := gcc
CFLAGS := -m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -ffreestanding -Wall -Wextra -O1 -I../../../includes -I../../../includes/ir0

.PHONY: all clean

all: panic.o
	@echo "\033[1;32m============================================================\033[0m"
	@echo "\033[1;32m Panic subsystem compilado correctamente para $(ARCH_NAME)\033[0m"
	@echo "\033[1;32m============================================================\033[0m"
panic.o: panic.c
	$(CC) $(CFLAGS) -c panic.c -o panic.o

clean:
	rm -f *.o