# kernelMain 

# drivers/timer/Makefile

PROJECT_ROOT := ../..
INCLUDES := -I$(PROJECT_ROOT)/includes -I$(PROJECT_ROOT)/includes/ir0

CFLAGS := -m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -ffreestanding -Wall -Wextra -O1 $(INCLUDES)

OBJS = Paging.o

paging: $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)
