# makefile de print

CC := gcc
CFLAGS := -m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -ffreestanding -Wall -Wextra -O1

all: print.o

print.o: print.c
	$(CC) $(CFLAGS) -c print.c -o print.o

clean:
	rm -f print.o
