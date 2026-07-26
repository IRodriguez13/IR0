# In-guest IR0 toolchain sample (TinyCC + GNU make).
CC ?= tcc
# TinyCC needs an explicit library path on IR0 rootfs.
CFLAGS ?= -B/lib/tcc -static -Os
hello: hello.c
	$(CC) $(CFLAGS) -o hello hello.c
clean:
	rm -f hello
