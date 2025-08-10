# drivers/timer/Makefile

PROJECT_ROOT := ../..
INCLUDES := -I$(PROJECT_ROOT)/includes -I$(PROJECT_ROOT)/includes/ir0

CFLAGS := -m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -ffreestanding -Wall -Wextra -O1 $(INCLUDES)

OBJS = pit/pit.o lapic/lapic.o hpet/hpet.o hpet/find_hpet.o clock_system.o best_clock.o

timers-complete: $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)



