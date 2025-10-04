# IO Drivers Makefile
IO_SOURCES = ps2.c ps2_mouse.c
IO_OBJECTS = $(IO_SOURCES:.c=.o)

# Compiler flags
CFLAGS += -I../../includes -I../../includes/ir0 -I../../arch/common -I../../setup

# Default target
all: $(IO_OBJECTS)

# Compile C files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean
clean:
	rm -f *.o

.PHONY: all clean
