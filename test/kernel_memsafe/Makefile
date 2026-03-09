#
# IR0 Kernel - kernel-memsafe
# Compila código del kernel para el host y ejecuta Valgrind para detectar
# leaks y errores de memoria en esas rutas. No ejecuta el kernel en QEMU.
#

KERNEL_ROOT ?= ../..
CC         = gcc
CFLAGS    := -Wall -Wextra -g -I. -I$(KERNEL_ROOT)/kernel -I$(KERNEL_ROOT)
LDFLAGS   =

KERNEL_SRC = $(KERNEL_ROOT)/kernel/resource_registry.c
KERNEL_OBJ = resource_registry.o
TEST_SRCS  = runner.c test_resource_registry.c
TEST_OBJS  = $(TEST_SRCS:.c=.o)
BIN        = ir0_kernel_memsafe

.PHONY: all clean run memsafe

all: $(BIN)

$(KERNEL_OBJ): $(KERNEL_SRC)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BIN): $(KERNEL_OBJ) $(TEST_OBJS)
	$(CC) -o $@ $(KERNEL_OBJ) $(TEST_OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

run: $(BIN)
	./$(BIN)

memsafe: $(BIN)
	valgrind --leak-check=full --error-exitcode=1 ./$(BIN)

clean:
	rm -f $(KERNEL_OBJ) $(TEST_OBJS) $(BIN)
