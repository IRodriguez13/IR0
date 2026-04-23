# IR0 Kernel - Test suite Makefile
# Solo se activa con make tests desde la raíz (o make -C test).
# Los tests se compilan con libc del host; no se usan includes del kernel
# para evitar conflictos (stdint, stdarg, etc.). Para tests que enlacen
# código del kernel haría falta un árbol de stubs.

KERNEL_ROOT ?= ..
CC         = gcc
CFLAGS     = -Wall -Wextra -g -I. -DTEST_HOST
LDFLAGS    =

TEST_SRCS  = test_runner.c test_example.c
TEST_OBJS  = $(TEST_SRCS:.c=.o)
TEST_BIN   = ir0_test_suite

.PHONY: all clean memsafe run

all: $(TEST_BIN)

$(TEST_BIN): $(TEST_OBJS)
	$(CC) -o $@ $(TEST_OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

run: $(TEST_BIN)
	./$(TEST_BIN)

memsafe: $(TEST_BIN)
	valgrind --leak-check=full --error-exitcode=1 ./$(TEST_BIN)

clean:
	rm -f $(TEST_OBJS) $(TEST_BIN)
