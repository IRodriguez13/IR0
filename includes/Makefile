# includes/Makefile - builds common include-side objects (e.g., string.o)

CC ?= gcc
ASM ?= nasm
CFLAGS ?= -Wall -Wextra -O1 -MMD -MP
ASMFLAGS ?= 

OBJS = string.o

all: $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -f $(OBJS)
	rm -f *.d

-include $(OBJS:.o=.d)


