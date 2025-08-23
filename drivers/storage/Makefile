# Storage Drivers Makefile
STORAGE_SOURCES = ata.c
STORAGE_OBJECTS = $(STORAGE_SOURCES:.c=.o)

# Compiler flags
CFLAGS += -I../../includes -I../../includes/ir0 -I../../arch/common -I../../setup

# Default target
all: $(STORAGE_OBJECTS)

# Compile C files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean
clean:
	rm -f *.o

.PHONY: all clean
