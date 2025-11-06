# Simple Makefile for userspace programs

CC = gcc
CFLAGS = -m64 -nostdlib -nostartfiles -nodefaultlibs -fno-builtin
CFLAGS += -fno-stack-protector -static -fPIC -O2
LDFLAGS = -nostdlib -static -Ttext=0x400000

PROGRAMS = hello

all: $(PROGRAMS)

hello: hello.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
	strip $@

clean:
	rm -f $(PROGRAMS)

.PHONY: all clean