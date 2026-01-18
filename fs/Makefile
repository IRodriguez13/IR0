# fs/Makefile - Filesystem Makefile

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
INCLUDES = -I../includes -I../includes/ir0 -I../mm

# Source files
VFS_SOURCES = vfs.c vfs_simple_wrappers.c
IR0FS_SOURCES = ir0fs.c
SOURCES = $(VFS_SOURCES) $(IR0FS_SOURCES)

# Object files
VFS_OBJECTS = $(VFS_SOURCES:.c=.o)
IR0FS_OBJECTS = $(IR0FS_SOURCES:.c=.o)
OBJECTS = $(SOURCES:.c=.o)

# Libraries
VFS_LIB = libvfs.a
IR0FS_LIB = libir0fs.a

# Default target
all: $(VFS_LIB) $(IR0FS_LIB)

# VFS library
$(VFS_LIB): $(VFS_OBJECTS)
	@echo "Creating VFS library..."
	ar rcs $@ $^

# IR0FS library
$(IR0FS_LIB): $(IR0FS_OBJECTS)
	@echo "Creating IR0FS library..."
	ar rcs $@ $^

# Compile source files
%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Clean
clean:
	@echo "Cleaning filesystem objects..."
	rm -f $(OBJECTS) $(VFS_LIB) $(IR0FS_LIB)

# Install
install: all
	@echo "Installing filesystem libraries..."
	cp $(VFS_LIB) ../lib/
	cp $(IR0FS_LIB) ../lib/

# Dependencies
vfs.o: vfs.c vfs.h ../includes/ir0/print.h ../includes/string.h ../memory/memo_interface.h
ir0fs.o: ir0fs.c ir0fs.h vfs.h ../includes/ir0/print.h ../includes/ir0/panic/panic.h ../includes/string.h ../memory/memo_interface.h

.PHONY: all clean install
