# Makefile ultra simple para ARM-32

# Compilador para ARM
CC = arm-none-eabi-gcc
AS = arm-none-eabi-as
LD = arm-none-eabi-ld
OBJCOPY = arm-none-eabi-objcopy

# Flags de compilación
CFLAGS = -mcpu=cortex-a7 -fpic -ffreestanding -O2 -Wall -Wextra -nostdlib -nostdinc
CFLAGS += -Iinclude
ASFLAGS = -mcpu=cortex-a7
LDFLAGS = -T linker.ld -nostdlib

# Archivos fuente
SOURCES = sources/arch_arm.c sources/uart_arm.c
OBJECTS = $(SOURCES:.c=.o)

# Target
TARGET = kernel-arm32.bin

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

test: $(TARGET)
	qemu-system-arm -M vexpress-a9 -cpu cortex-a9 -kernel $(TARGET) -display gtk -m 128M

test-nographic: $(TARGET)
	qemu-system-arm -M vexpress-a9 -cpu cortex-a9 -kernel $(TARGET) -nographic -m 128M

test-uart: $(TARGET)
	qemu-system-arm -M vexpress-a9 -cpu cortex-a9 -kernel $(TARGET) -nographic -m 128M -serial pty

.PHONY: all clean test test-nographic test-uart
