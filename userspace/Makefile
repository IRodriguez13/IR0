# Makefile for IR0 userspace programs
CC = gcc
CFLAGS = -m64 -nostdlib -nostartfiles -nodefaultlibs -fno-builtin -fno-stack-protector
CFLAGS += -Ilibc/include -I../includes -static -fPIC
LDFLAGS = -nostdlib -static

# Directories
LIBC_SRC = libc/src
LIBC_INC = libc/include
BIN_SRC = bin
BUILD_DIR = build

# Libc object files
LIBC_OBJS = $(BUILD_DIR)/syscalls.o $(BUILD_DIR)/stdio.o $(BUILD_DIR)/malloc.o

# Programs
PROGRAMS = $(BUILD_DIR)/echo

.PHONY: all clean

all: $(BUILD_DIR) $(PROGRAMS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Build libc objects
$(BUILD_DIR)/%.o: $(LIBC_SRC)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Build echo program
$(BUILD_DIR)/echo: $(BIN_SRC)/echo.c $(LIBC_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

clean:
	rm -rf $(BUILD_DIR)

install: all
	# Copy programs to filesystem (when we have one)
	@echo "Programs built in $(BUILD_DIR)/"