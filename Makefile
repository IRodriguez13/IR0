# IR0 master kernel makefile

CC = gcc
ASM = nasm
LD = ld

CFLAGS = -m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -fno-pic -nodefaultlibs -ffreestanding -Iincludes -Iincludes/ir0 -Wall -Wextra -O1 -MMD -MP
ASMFLAGS = -f elf32 # ESTO VA A CAMBIAR EN EL MAKE DE 64 BIT O CON CONDICIONALES.
LDFLAGS = -m elf_i386 -T linker.ld

# Llama a los Makefiles de cada subsistema
SUBDIRS = kernel boot interrupt drivers/timer paging scheduler includes/ir0/panic 

.PHONY: all clean $(SUBDIRS)

all: subsystems kernel.bin kernel.iso

# Opción recomendada - sin backslash al final
subsystems: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

# Archivos objeto principales
OBJS = kernel/kernel.o \
       boot/boot.o \
       interrupt/idt.o interrupt/interrupt.o interrupt/isr_handlers.o \
       includes/print.o \
       includes/string.o \
       paging/Paging.o \
       panic/panic.o \
       drivers/timer/pit/pit.o \
       drivers/timer/clock_system.o \
       drivers/timer/best_clock.o \
       drivers/timer/acpi/acpi.o \
       drivers/timer/hpet/hpet.o \
       drivers/timer/hpet/find_hpet.o \
       drivers/timer/lapic/lapic.o \
       scheduler/scheduler.o \
       scheduler/switch/switch.o \
       

kernel.bin: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

kernel.iso: kernel.bin
	@mkdir -p iso/boot/grub
	@cp kernel.bin iso/boot/

clean:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done
	rm -f $(OBJS) kernel.bin kernel.iso

distclean: clean

run: kernel.iso
	@echo "acá podría correr QEMU con el kernel cargago, pero eso ya lo manejo en el menu.sh"

-include $(OBJS:.o=.P)