# Makefile de arch/common

OBJS = arch_interface.o
CC ?= gcc
CFLAGS ?= -Wall -Wextra

all: $(OBJS)

arch_interface.o: arch_interface.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(OBJS:.o=.d)
