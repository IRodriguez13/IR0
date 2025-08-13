# IR0 master kernel makefile - Multi-architecture support

KERNEL_ROOT := $(CURDIR)

# Arquitectura por defecto
ARCH ?= x86-32

COMMON_SUBDIRS = kernel interrupt drivers/timer scheduler includes/ir0/panic arch/common memory
ifeq ($(ARCH),x_64)
    # Configuración para 64-bit
    CC = gcc
    ASM = nasm  
    LD = ld
    CFLAGS = -m64 -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2
    CFLAGS += -nostdlib -nostdinc -fno-builtin -fno-stack-protector -fno-pic -nodefaultlibs -ffreestanding
    CFLAGS += -I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 -I$(KERNEL_ROOT)/arch/common -I$(KERNEL_ROOT)/arch/x_64/include -Wall -Wextra -O1 -MMD -MP
    ASMFLAGS = -f elf64
    LDFLAGS = -m elf_x86_64 -T arch/x_64/linker.ld
    ARCH_SUBDIRS = arch/x_64
    KERNEL_ENTRY = kmain_x64
else ifeq ($(ARCH),x86-32)
    # Configuración para 32-bit
    CC = gcc
    ASM = nasm
    LD = ld  
    CFLAGS = -m32 -march=i686 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -fno-pic -nodefaultlibs -ffreestanding
    CFLAGS += -I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 -I$(KERNEL_ROOT)/arch/common -I$(KERNEL_ROOT)/arch/x86-32/include -Wall -Wextra -O1 -MMD -MP
    ASMFLAGS = -f elf32
    LDFLAGS = -m elf_i386 -T arch/x86-32/linker.ld
    ARCH_SUBDIRS = arch/x86-32
    KERNEL_ENTRY = kmain_x32
else
    $(error Arquitectura no soportada: $(ARCH). Use x_64 o x86-32)
endif

SUBDIRS = $(COMMON_SUBDIRS) $(ARCH_SUBDIRS)

.PHONY: all clean $(SUBDIRS) $(ARCH_SUBDIRS) arch-info

all: arch-info subsystems kernel-$(ARCH).bin kernel-$(ARCH).iso

arch-info:
	@echo "Compilando para arquitectura: $(ARCH)"
	@echo "Directorio de arquitectura: $(ARCH_SUBDIRS)"
	@echo "Flags C: $(CFLAGS)"
	@echo "Flags ASM: $(ASMFLAGS)"
	@echo "Flags LD: $(LDFLAGS)"
	@echo ""

subsystems: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@ CC="$(CC)" ASM="$(ASM)" CFLAGS="$(CFLAGS)" ASMFLAGS="$(ASMFLAGS)"

# En la sección COMMON_SUBDIRS, cambiar:
COMMON_SUBDIRS = kernel interrupt drivers/timer paging scheduler includes/ir0/panic arch/common memory

# En la sección de objetos del kernel, agregar los objetos de memoria:
KERNEL_OBJS = kernel/kernel_start.o \
              includes/print.o \
              includes/string.o \
              interrupt/idt.o interrupt/isr_handlers.o \
              includes/ir0/panic/panic.o \
              drivers/timer/pit/pit.o \
              drivers/timer/clock_system.o \
              drivers/timer/best_clock.o \
              drivers/timer/acpi/acpi.o \
              drivers/timer/hpet/hpet.o \
              drivers/timer/hpet/find_hpet.o \
              drivers/timer/lapic/lapic.o \
              scheduler/scheduler.o \
              scheduler/dummy_tasks.o \
              scheduler/switch/switch.o \
              arch/common/arch_interface.o \
              memory/heap_allocator.o \
              memory/physical_allocator.o


# Para arquitectura específica, actualizar ARCH_OBJS:
ifeq ($(ARCH),x_64)
    ARCH_OBJS = $(ARCH_SUBDIRS)/arch.o \
                $(ARCH_SUBDIRS)/boot.o \
                $(ARCH_SUBDIRS)/sources/idt_arch_x64.o \
                $(ARCH_SUBDIRS)/sources/fault.o \
                memory/arch/x_64/Paging_x64.o \
                memory/arch/x_64/mmu_x64.o
else ifeq ($(ARCH),x86-32)  
    ARCH_OBJS = $(ARCH_SUBDIRS)/arch.o \
                $(ARCH_SUBDIRS)/boot.o \
                $(ARCH_SUBDIRS)/sources/idt_arch_x86.o \
                memory/arch/x_86-32/Paging_x86.o \
                memory/arch/x_86-32/mmu_x86.o
endif

# Todos los objetos
ALL_OBJS = $(KERNEL_OBJS) $(ARCH_OBJS)

# Compilar kernel para arquitectura específica
kernel-$(ARCH).bin: $(ALL_OBJS) $(ARCH_SUBDIRS)/linker.ld
	@echo "Enlazando kernel para $(ARCH)..."
	$(LD) $(LDFLAGS) -o $@ $(ALL_OBJS)
	@echo "Kernel $(ARCH) compilado: $@"

# Crear ISO específico por arquitectura
kernel-$(ARCH).iso: kernel-$(ARCH).bin
	@echo "Creando ISO para $(ARCH)..."
	@mkdir -p iso-$(ARCH)/boot/grub
	@cp kernel-$(ARCH).bin iso-$(ARCH)/boot/
	@cp $(ARCH_SUBDIRS)/grub.cfg iso-$(ARCH)/boot/grub/grub.cfg
	@if command -v grub-mkrescue >/dev/null 2>&1; then \
		grub-mkrescue -o $@ iso-$(ARCH); \
		echo "ISO creada: $@"; \
	else \
		echo "grub-mkrescue no encontrado. ISO no creada."; \
	fi

# Ejecutar con QEMU
run: kernel-$(ARCH).iso
	@echo "Ejecutando kernel $(ARCH) en QEMU..."
ifeq ($(ARCH),x86-64)
	qemu-system-x86_64 -cdrom kernel-$(ARCH).iso -m 512M
else ifeq ($(ARCH),x86-32)
	qemu-system-i386 -cdrom kernel-$(ARCH).iso -m 512M
endif

# Ejecutar con bochs (si disponible)
run-bochs: kernel-$(ARCH).bin
	@echo "Ejecutando kernel $(ARCH) en Bochs..."
	@echo "bochs -f bochsrc-$(ARCH)"

# Compilar ambas arquitecturas
all-arch:
	@echo "Compilando todas las arquitecturas..."
	$(MAKE) ARCH=x86-64 clean all
	$(MAKE) ARCH=x_64 clean all
	@echo "Compilación completa para todas las arquitecturas"

# Limpieza
clean:
	@echo "Limpiando arquitectura: $(ARCH)"
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done
	@if [ -d "$(ARCH_SUBDIRS)" ]; then \
		$(MAKE) -C $(ARCH_SUBDIRS) clean; \
	fi
	rm -f $(ALL_OBJS) $(ALL_OBJS:.o=.d) kernel-$(ARCH).bin kernel-$(ARCH).iso
	rm -rf iso-$(ARCH)


# Limpieza completa (todas las arquitecturas)
clean-all:
	@echo "Limpieza completa..."
	$(MAKE) ARCH=x86-64 clean
	$(MAKE) ARCH=x_64 clean
	rm -f *.bin *.iso
	rm -rf iso-*

distclean: clean-all

# Información de ayuda
help:
	@echo "IR0 Kernel Multi-Architecture Build System"
	@echo ""
	@echo "Arquitecturas soportadas:"
	@echo "  x86-64  - 32-bit (i386)"
	@echo "  x_64    - 64-bit (x86_64)"
	@echo ""
	@echo "Comandos principales:"
	@echo "  make                    - Compilar para arquitectura por defecto (x86-64)"
	@echo "  make ARCH=x_64          - Compilar para 64-bit"
	@echo "  make ARCH=x86-64        - Compilar para 32-bit"
	@echo "  make all-arch           - Compilar para todas las arquitecturas"
	@echo "  make run                - Ejecutar en QEMU"
	@echo "  make clean              - Limpiar arquitectura actual"
	@echo "  make clean-all          - Limpiar todas las arquitecturas"
	@echo "  make help               - Mostrar esta ayuda"
	@echo ""
	@echo "Ejemplos:"
	@echo "  make ARCH=x_64 run      - Compilar y ejecutar en 64-bit"
	@echo "  make ARCH=x86-64 run    - Compilar y ejecutar en 32-bit"

-include $(ALL_OBJS:.o=.d)