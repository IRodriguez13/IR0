CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -ffreestanding -O2 -nostdlib -nostartfiles -nodefaultlibs
CFLAGS += -I../../includes -I../../
CFLAGS += -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2

OBJECTS = sound_blaster.o

all: libaudio.a

libaudio.a: $(OBJECTS)
	ar rcs libaudio.a $(OBJECTS)

sound_blaster.o: sound_blaster.c sound_blaster.h
	$(CC) $(CFLAGS) -c sound_blaster.c -o sound_blaster.o

clean:
	rm -f *.o *.a *.d

.PHONY: all clean