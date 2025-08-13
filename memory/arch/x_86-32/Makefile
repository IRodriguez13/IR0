PROJECT_ROOT := ../../..

CC ?= gcc
CFLAGS := -m32 -march=i686 -nostdlib -nostdinc -fno-builtin -fno-stack-protector
CFLAGS += -ffreestanding -Wall -Wextra -O1 -MMD -MP
CFLAGS += -I$(PROJECT_ROOT)/includes -I$(PROJECT_ROOT)/includes/ir0
CFLAGS += -I$(PROJECT_ROOT)/arch/common -I$(PROJECT_ROOT)/arch/x86-32/include

SOURCES = Paging_x86-32.c mmu_x86-32.c
OBJECTS = $(SOURCES:.c=.o)

all: $(OBJECTS)
	@echo "\033[1;32m============================================================\033[0m"
	@echo "\033[1;32m MM subsystem compilado correctamente para x86-32\033[0m"
	@echo "\033[1;32m============================================================\033[0m"

%.o: %.c
	@echo "Compilando $< (x86-32)..."
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(OBJECTS:.o=.d)
	@echo "Limpieza x86-32 memoria completada"

-include $(OBJECTS:.o=.d)
