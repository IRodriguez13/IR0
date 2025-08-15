# IR0 master kernel makefile - Multi-architecture support with conditional compilation
KERNEL_ROOT := $(CURDIR)

# Arquitectura por defecto
ARCH ?= x86-32

# Build target por defecto
BUILD_TARGET ?= desktop

# Validar build target
VALID_TARGETS = desktop server iot embedded
ifeq ($(filter $(BUILD_TARGET),$(VALID_TARGETS)),)
    $(error Build target no válido: $(BUILD_TARGET). Use: desktop, server, iot, embedded)
endif

# Validar arquitectura
VALID_ARCHS = x86-32 x86-64 arm32 arm64
ifeq ($(filter $(ARCH),$(VALID_ARCHS)),)
    $(error Arquitectura no soportada: $(ARCH). Use: x86-32, x86-64, arm32, arm64)
endif

# Configurar flags según build target
ifeq ($(BUILD_TARGET),desktop)
    CFLAGS_TARGET = -DIR0_DESKTOP
    TARGET_NAME = desktop
else ifeq ($(BUILD_TARGET),server)
    CFLAGS_TARGET = -DIR0_SERVER
    TARGET_NAME = server
else ifeq ($(BUILD_TARGET),iot)
    CFLAGS_TARGET = -DIR0_IOT
    TARGET_NAME = iot
else ifeq ($(BUILD_TARGET),embedded)
    CFLAGS_TARGET = -DIR0_EMBEDDED
    TARGET_NAME = embedded
endif

# Incluir configuración del kernel (solo para obtener versiones)
# Las definiciones se pasan via CFLAGS_TARGET

# Información de versión del kernel
IR0_VERSION_MAJOR := 1
IR0_VERSION_MINOR := 0
IR0_VERSION_PATCH := 0
IR0_VERSION_STRING := $(IR0_VERSION_MAJOR).$(IR0_VERSION_MINOR).$(IR0_VERSION_PATCH)
IR0_BUILD_DATE := $(shell date +%Y-%m-%d)
IR0_BUILD_TIME := $(shell date +%H:%M:%S)

# Subsistemas comunes (siempre presentes)
COMMON_SUBDIRS = kernel interrupt drivers/timer kernel/scheduler includes/ir0/panic arch/common memory setup

# Subsistemas condicionales según build target
ifeq ($(BUILD_TARGET),desktop)
    CONDITIONAL_SUBDIRS = fs
else ifeq ($(BUILD_TARGET),server)
    CONDITIONAL_SUBDIRS = fs
else ifeq ($(BUILD_TARGET),iot)
    CONDITIONAL_SUBDIRS = fs
else ifeq ($(BUILD_TARGET),embedded)
    CONDITIONAL_SUBDIRS = 
endif

SUBDIRS = $(COMMON_SUBDIRS) $(CONDITIONAL_SUBDIRS) $(ARCH_SUBDIRS)

ifeq ($(ARCH),x86-64)
    # Configuración para 64-bit
    CC = gcc
    ASM = nasm  
    LD = ld
    CFLAGS = -m64 -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2
    CFLAGS += -nostdlib -nostdinc -fno-builtin -fno-stack-protector -fno-pic -nodefaultlibs -ffreestanding
    CFLAGS += -I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 -I$(KERNEL_ROOT)/arch/common -I$(KERNEL_ROOT)/arch/x86-64/include -I$(KERNEL_ROOT)/setup
    CFLAGS += -Wall -Wextra -O1 -MMD -MP $(CFLAGS_TARGET)
    ASMFLAGS = -f elf64
    LDFLAGS = -m elf_x86_64 -T arch/x86-64/linker.ld
    ARCH_SUBDIRS = arch/x86-64
    KERNEL_ENTRY = kmain_x64
else ifeq ($(ARCH),x86-32)
    # Configuración para 32-bit
    CC = gcc
    ASM = nasm
    LD = ld  
    CFLAGS = -m32 -march=i686 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -fno-pic -nodefaultlibs -ffreestanding
    CFLAGS += -I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 -I$(KERNEL_ROOT)/arch/common -I$(KERNEL_ROOT)/arch/x86-32/include -I$(KERNEL_ROOT)/setup
    CFLAGS += -Wall -Wextra -O1 -MMD -MP $(CFLAGS_TARGET)
    ASMFLAGS = -f elf32
    LDFLAGS = -m elf_i386 -T arch/x86-32/linker.ld
    ARCH_SUBDIRS = arch/x86-32
    KERNEL_ENTRY = kmain_x32
else ifeq ($(ARCH),arm64)
    # Configuración para ARM64
    CC = aarch64-linux-gnu-gcc
    ASM = aarch64-linux-gnu-as
    LD = aarch64-linux-gnu-ld
    CFLAGS = -march=armv8-a -nostdlib -nostdinc -fno-builtin -fno-stack-protector -fno-pic -nodefaultlibs -ffreestanding
    CFLAGS += -I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 -I$(KERNEL_ROOT)/arch/common -I$(KERNEL_ROOT)/arch/arm64/include -I$(KERNEL_ROOT)/setup
    CFLAGS += -Wall -Wextra -O1 -MMD -MP $(CFLAGS_TARGET)
    ASMFLAGS = --64
    LDFLAGS = -m aarch64linux -T arch/arm64/linker.ld
    ARCH_SUBDIRS = arch/arm64
    KERNEL_ENTRY = kmain_arm64
else ifeq ($(ARCH),arm32)
    # Configuración para ARM32
    CC = arm-linux-gnueabi-gcc
    ASM = arm-linux-gnueabi-as
    LD = arm-linux-gnueabi-ld
    CFLAGS = -march=armv7-a -mthumb -nostdlib -nostdinc -fno-builtin -fno-stack-protector -fno-pic -nodefaultlibs -ffreestanding
    CFLAGS += -I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 -I$(KERNEL_ROOT)/arch/common -I$(KERNEL_ROOT)/arch/arm32/include -I$(KERNEL_ROOT)/setup
    CFLAGS += -Wall -Wextra -O1 -MMD -MP $(CFLAGS_TARGET)
    ASMFLAGS = --32
    LDFLAGS = -m armelf_linux_eabi -T arch/arm32/linker.ld
    ARCH_SUBDIRS = arch/arm32
    KERNEL_ENTRY = kmain_arm32
endif

.PHONY: all clean $(SUBDIRS) $(ARCH_SUBDIRS) arch-info build-info help all-arch all-targets

all: build-info arch-info subsystems kernel-$(ARCH)-$(TARGET_NAME).bin kernel-$(ARCH)-$(TARGET_NAME).iso

# Target para compilar todas las arquitecturas
all-arch: 
	@echo "Compilando para todas las arquitecturas..."
	@$(MAKE) ARCH=x86-32 BUILD_TARGET=$(BUILD_TARGET)
	@$(MAKE) ARCH=x86-64 BUILD_TARGET=$(BUILD_TARGET)
	@echo "Compilación completa para todas las arquitecturas"

# Target para compilar todos los build targets
all-targets:
	@echo "Compilando para todos los build targets..."
	@$(MAKE) ARCH=$(ARCH) BUILD_TARGET=desktop
	@$(MAKE) ARCH=$(ARCH) BUILD_TARGET=server
	@$(MAKE) ARCH=$(ARCH) BUILD_TARGET=iot
	@$(MAKE) ARCH=$(ARCH) BUILD_TARGET=embedded
	@echo "Compilación completa para todos los build targets"

# Target para compilar todas las combinaciones
all-combinations:
	@echo "Compilando todas las combinaciones de arquitectura y build target..."
	@$(MAKE) ARCH=x86-32 BUILD_TARGET=desktop
	@$(MAKE) ARCH=x86-32 BUILD_TARGET=server
	@$(MAKE) ARCH=x86-32 BUILD_TARGET=iot
	@$(MAKE) ARCH=x86-32 BUILD_TARGET=embedded
	@$(MAKE) ARCH=x86-64 BUILD_TARGET=desktop
	@$(MAKE) ARCH=x86-64 BUILD_TARGET=server
	@$(MAKE) ARCH=x86-64 BUILD_TARGET=iot
	@$(MAKE) ARCH=x86-64 BUILD_TARGET=embedded
	@echo "Compilación completa para todas las combinaciones"

build-info:
	@echo "============================================================"
	@echo "IR0 Kernel Build System"
	@echo "============================================================"
	@echo "Build Target: $(BUILD_TARGET) ($(TARGET_NAME))"
	@echo "Architecture: $(ARCH)"
	@echo "Version: $(IR0_VERSION_STRING)"
	@echo "Build Date: $(IR0_BUILD_DATE)"
	@echo "Build Time: $(IR0_BUILD_TIME)"
	@echo ""

arch-info:
	@echo "Compilando para arquitectura: $(ARCH)"
	@echo "Directorio de arquitectura: $(ARCH_SUBDIRS)"
	@echo "Flags C: $(CFLAGS)"
	@echo "Flags ASM: $(ASMFLAGS)"
	@echo "Flags LD: $(LDFLAGS)"
	@echo ""

subsystems: $(SUBDIRS)
	@echo ""
	@echo "\033[1;32m============================================================\033[0m"
	@echo "\033[1;32m Todos los subsistemas compilados correctamente para $(ARCH)-$(TARGET_NAME)\033[0m"
	@echo "\033[1;32m============================================================\033[0m"
	@echo ""

$(SUBDIRS):
	$(MAKE) -C $@ CC="$(CC)" ASM="$(ASM)" CFLAGS="$(CFLAGS)" ASMFLAGS="$(ASMFLAGS)"

# Objetos del kernel base
KERNEL_BASE_OBJS = kernel/kernel_start.o \
                   includes/ir0/print.o \
                   includes/string.o \
                   interrupt/idt.o interrupt/isr_handlers.o \
                   includes/ir0/panic/panic.o \
                   drivers/timer/pit/pit.o \
                   drivers/timer/clock_system.o \
                   drivers/timer/best_clock.o \
                   drivers/timer/hpet/hpet.o \
                   drivers/timer/hpet/find_hpet.o \
                   drivers/timer/lapic/lapic.o \
                   kernel/scheduler/priority_scheduler.o \
                   kernel/scheduler/round-robin_scheduler.o \
                   kernel/scheduler/sched_central.o \
                   kernel/scheduler/cfs_scheduler.o \
                   kernel/scheduler/scheduler_detection.o \
                   kernel/scheduler/task_impl.o \
                   arch/common/arch_interface.o \
                   memory/heap_allocator.o \
                   memory/physical_allocator.o \
                   memory/ondemand-paging.o \
                   memory/vallocator.o \
                   setup/kernel_config.o

# Objetos condicionales según build target
ifeq ($(BUILD_TARGET),desktop)
    CONDITIONAL_OBJS = fs/vfs_simple.o
else ifeq ($(BUILD_TARGET),server)
    CONDITIONAL_OBJS = fs/vfs_simple.o
else ifeq ($(BUILD_TARGET),iot)
    CONDITIONAL_OBJS = fs/vfs_simple.o
else ifeq ($(BUILD_TARGET),embedded)
    CONDITIONAL_OBJS = 
endif

# Objetos de arquitectura
ifeq ($(ARCH),x86-64)
    ARCH_OBJS = $(ARCH_SUBDIRS)/sources/arch_x64.o \
                $(ARCH_SUBDIRS)/asm/boot_x64.o \
                $(ARCH_SUBDIRS)/sources/idt_arch_x64.o \
                $(ARCH_SUBDIRS)/sources/fault.o \
                memory/arch/x86-64/Paging_x64.o \
                memory/arch/x86-64/mmu_x64.o \
                kernel/scheduler/switch/switch_x64.o \
                interrupt/arch/x86-64/interrupt.o
else ifeq ($(ARCH),x86-32)  
    ARCH_OBJS = $(ARCH_SUBDIRS)/sources/arch_x86.o \
                $(ARCH_SUBDIRS)/asm/boot_x86.o \
                memory/arch/x_86-32/Paging_x86-32.o \
                memory/arch/x_86-32/mmu_x86-32.o \
                kernel/scheduler/switch/switch_x86.o \
                interrupt/arch/x86-32/interrupt.o
else ifeq ($(ARCH),arm64)
    ARCH_OBJS = $(ARCH_SUBDIRS)/sources/arch_arm64.o \
                $(ARCH_SUBDIRS)/asm/boot_arm64.o \
                $(ARCH_SUBDIRS)/sources/exception_arm64.o \
                memory/arch/arm64/paging_arm64.o \
                memory/arch/arm64/mmu_arm64.o \
                kernel/scheduler/switch/switch_arm64.o \
                interrupt/arch/arm64/interrupt.o
else ifeq ($(ARCH),arm32)
    ARCH_OBJS = $(ARCH_SUBDIRS)/sources/arch_arm32.o \
                $(ARCH_SUBDIRS)/asm/boot_arm32.o \
                $(ARCH_SUBDIRS)/sources/exception_arm32.o \
                memory/arch/arm32/paging_arm32.o \
                memory/arch/arm32/mmu_arm32.o \
                kernel/scheduler/switch/switch_arm32.o \
                interrupt/arch/arm32/interrupt.o
endif

# Todos los objetos
ALL_OBJS = $(KERNEL_BASE_OBJS) $(CONDITIONAL_OBJS) $(ARCH_OBJS)

# Compilar kernel para arquitectura específica
kernel-$(ARCH)-$(TARGET_NAME).bin: $(ALL_OBJS) $(ARCH_SUBDIRS)/linker.ld
	@echo "Enlazando kernel para $(ARCH)-$(TARGET_NAME)..."
	$(LD) $(LDFLAGS) -o $@ $(ALL_OBJS)
	@echo "Kernel $(ARCH)-$(TARGET_NAME) compilado: $@"

# Crear ISO específico por arquitectura y target
kernel-$(ARCH)-$(TARGET_NAME).iso: kernel-$(ARCH)-$(TARGET_NAME).bin
	@echo "Creando ISO para $(ARCH)-$(TARGET_NAME)..."
	@mkdir -p iso-$(ARCH)-$(TARGET_NAME)/boot/grub
	@cp $(ARCH_SUBDIRS)/grub.cfg iso-$(ARCH)-$(TARGET_NAME)/boot/grub/
	@cp kernel-$(ARCH)-$(TARGET_NAME).bin iso-$(ARCH)-$(TARGET_NAME)/boot/
	@grub-mkrescue -o $@ iso-$(ARCH)-$(TARGET_NAME)
	@echo "ISO $(ARCH)-$(TARGET_NAME) creado: $@"

# Target para ejecutar en QEMU
run: kernel-$(ARCH)-$(TARGET_NAME).iso
	@echo "Ejecutando kernel $(ARCH)-$(TARGET_NAME) en QEMU..."
ifeq ($(ARCH),x86-64)
	qemu-system-x86_64 -cdrom kernel-$(ARCH)-$(TARGET_NAME).iso -m 512M -display gtk
else ifeq ($(ARCH),x86-32)
	qemu-system-i386 -cdrom kernel-$(ARCH)-$(TARGET_NAME).iso -m 512M -display gtk
else ifeq ($(ARCH),arm64)
	qemu-system-aarch64 -M virt -cpu cortex-a57 -m 512M -kernel kernel-$(ARCH)-$(TARGET_NAME).bin -display gtk
else ifeq ($(ARCH),arm32)
	qemu-system-arm -M vexpress-a9 -cpu cortex-a9 -m 512M -kernel kernel-$(ARCH)-$(TARGET_NAME).bin -display gtk
endif

# Target para debug en QEMU
debug: kernel-$(ARCH)-$(TARGET_NAME).iso
	@echo "Ejecutando kernel $(ARCH)-$(TARGET_NAME) en QEMU con debug..."
ifeq ($(ARCH),x86-64)
	qemu-system-x86_64 -cdrom kernel-$(ARCH)-$(TARGET_NAME).iso -m 512M -display gtk -d int,cpu_reset -D qemu_debug.log
else
	qemu-system-i386 -cdrom kernel-$(ARCH)-$(TARGET_NAME).iso -m 512M -display gtk -d int,cpu_reset -D qemu_debug.log
endif

# Limpiar archivos de compilación
clean:
	@echo "Limpiando archivos de compilación..."
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done
	@rm -f kernel-*.bin kernel-*.iso
	@rm -rf iso-*
	@echo "Limpieza completada"

# Limpiar todo (incluyendo logs)
clean-all: clean
	@echo "Limpiando archivos adicionales..."
	@rm -f *.log
	@rm -f qemu_*.log
	@echo "Limpieza completa finalizada"

# Mostrar información de arquitectura
arch-details:
	@echo "Arquitectura actual: $(ARCH)"
	@echo "Compilador: $(CC)"
	@echo "Ensamblador: $(ASM)"
	@echo "Linker: $(LD)"
	@echo "Flags C: $(CFLAGS)"
	@echo "Flags ASM: $(ASMFLAGS)"
	@echo "Flags LD: $(LDFLAGS)"

# Información de ayuda
help:
	@echo "IR0 Kernel Multi-Architecture Build System"
	@echo ""
	@echo "Arquitecturas soportadas:"
	@echo "  x86-32  - 32-bit (i386)"
	@echo "  x86-64  - 64-bit (x86_64)"
	@echo "  arm32   - ARM 32-bit (ARMv7)"
	@echo "  arm64   - ARM 64-bit (ARMv8)"
	@echo ""
	@echo "Build Targets soportados:"
	@echo "  desktop   - Sistema de escritorio"
	@echo "  server    - Sistema servidor"
	@echo "  iot       - Sistema IoT"
	@echo "  embedded  - Sistema embebido"
	@echo ""
	@echo "Comandos principales:"
	@echo "  make                    - Compilar para arquitectura por defecto (x86-32)"
	@echo "  make ARCH=x86-64          - Compilar para 64-bit"
	@echo "  make ARCH=x86-32        - Compilar para 32-bit"
	@echo "  make all-arch           - Compilar para todas las arquitecturas"
	@echo "  make all-targets        - Compilar para todos los build targets"
	@echo "  make all-combinations   - Compilar todas las combinaciones"
	@echo "  make run                - Ejecutar en QEMU"
	@echo "  make clean              - Limpiar arquitectura actual"
	@echo "  make clean-all          - Limpiar todas las arquitecturas"
	@echo "  make help               - Mostrar esta ayuda"
	@echo ""
	@echo "Ejemplos:"
	@echo "  make ARCH=x86-64 run      - Compilar y ejecutar en 64-bit"
	@echo "  make ARCH=x86-32 run    - Compilar y ejecutar en 32-bit"
	@echo "  make BUILD_TARGET=server - Compilar versión servidor"

# Targets específicos por build target
desktop: BUILD_TARGET=desktop
desktop: all

server: BUILD_TARGET=server
server: all

iot: BUILD_TARGET=iot
iot: all

embedded: BUILD_TARGET=embedded
embedded: all

# Targets específicos por arquitectura
x86-64: ARCH=x86-64
x86-64: all

x86-32: ARCH=x86-32
x86-32: all

-include $(ALL_OBJS:.o=.d)