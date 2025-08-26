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

# Información de versión del kernel
IR0_VERSION_MAJOR := 1
IR0_VERSION_MINOR := 0
IR0_VERSION_PATCH := 0
IR0_VERSION_STRING := $(IR0_VERSION_MAJOR).$(IR0_VERSION_MINOR).$(IR0_VERSION_PATCH)
IR0_BUILD_DATE := $(shell date +%Y-%m-%d)
IR0_BUILD_TIME := $(shell date +%H:%M:%S)

# Configuración por arquitectura
ifeq ($(ARCH),x86-64)
    # Configuración para 64-bit
    CC = gcc
    ASM = nasm  
    LD = ld
    CFLAGS = -m64 -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -D__x86_64__
    CFLAGS += -nostdlib -nostdinc -fno-builtin -fno-stack-protector -fno-pic -nodefaultlibs -ffreestanding
    CFLAGS += -I$(KERNEL_ROOT) -I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 -I$(KERNEL_ROOT)/arch/common -I$(KERNEL_ROOT)/arch/x86-64/include -I$(KERNEL_ROOT)/arch/x86-64/sources -I$(KERNEL_ROOT)/setup -I$(KERNEL_ROOT)/memory -I$(KERNEL_ROOT)/memory/arch/x86-64 -I$(KERNEL_ROOT)/interrupt -I$(KERNEL_ROOT)/drivers -I$(KERNEL_ROOT)/fs -I$(KERNEL_ROOT)/kernel -I$(KERNEL_ROOT)/examples
    CFLAGS += -Wall -Wextra -O0 -MMD -MP $(CFLAGS_TARGET)
    ASMFLAGS = -f elf64
    LDFLAGS = -m elf_x86_64 -T arch/x86-64/linker.ld
    ARCH_SUBDIRS = arch/x86-64
    KERNEL_ENTRY = kmain_x64
else ifeq ($(ARCH),x86-32)
    # Configuración para 32-bit
    CC = gcc
    ASM = nasm
    LD = ld  
    CFLAGS = -m32 -march=i686 -D__i386__ -nostdlib -nostdinc -fno-builtin -fno-stack-protector -fno-pic -nodefaultlibs -ffreestanding
    CFLAGS += -I$(KERNEL_ROOT) -I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 -I$(KERNEL_ROOT)/arch/common -I$(KERNEL_ROOT)/arch/x86-32/include -I$(KERNEL_ROOT)/arch/x86-32/sources -I$(KERNEL_ROOT)/setup -I$(KERNEL_ROOT)/memory -I$(KERNEL_ROOT)/memory/arch/x86-32 -I$(KERNEL_ROOT)/interrupt -I$(KERNEL_ROOT)/drivers -I$(KERNEL_ROOT)/fs -I$(KERNEL_ROOT)/kernel -I$(KERNEL_ROOT)/examples
    CFLAGS += -Wall -Wextra -O0 -MMD -MP $(CFLAGS_TARGET)
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
    CFLAGS += -I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 -I$(KERNEL_ROOT)/arch/common -I$(KERNEL_ROOT)/arch/arm64/include -I$(KERNEL_ROOT)/setup -I$(KERNEL_ROOT)/memory
    CFLAGS += -Wall -Wextra -O0 -MMD -MP $(CFLAGS_TARGET)
    ASMFLAGS = --64
    LDFLAGS = -m aarch64linux -T arch/arm64/linker.ld
    ARCH_SUBDIRS = arch/arm64
    KERNEL_ENTRY = kmain_arm64
else ifeq ($(ARCH),arm32)
    # Configuración para ARM32
    CC = arm-none-eabi-gcc
    ASM = arm-none-eabi-as
    LD = arm-none-eabi-ld
    CFLAGS = -march=armv7-a -nostdlib -nostdinc -fno-builtin -fno-stack-protector -fno-pic -nodefaultlibs -ffreestanding
    CFLAGS += -I$(KERNEL_ROOT) -I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 -I$(KERNEL_ROOT)/arch/common -I$(KERNEL_ROOT)/arch/arm-32/include -I$(KERNEL_ROOT)/arch/arm-32/sources -I$(KERNEL_ROOT)/setup -I$(KERNEL_ROOT)/memory -I$(KERNEL_ROOT)/interrupt -I$(KERNEL_ROOT)/drivers -I$(KERNEL_ROOT)/fs -I$(KERNEL_ROOT)/kernel -I$(KERNEL_ROOT)/examples
    CFLAGS += -Wall -Wextra -O0 -MMD -MP $(CFLAGS_TARGET)
    ASMFLAGS = --32
    LDFLAGS = -m armelf_linux_eabi -T arch/arm-32/linker.ld
    ARCH_SUBDIRS = arch/arm-32
    KERNEL_ENTRY = kmain_arm32
endif

# Subsistemas comunes (siempre presentes)
COMMON_SUBDIRS = kernel interrupt drivers/timer drivers/IO drivers/storage kernel/scheduler includes includes/ir0 includes/ir0/panic arch/common memory setup

# Subsistemas condicionales según build target

# ===============================================================================
# CONFIGURACIÓN QEMU (ABSTRACCIÓN)
# ===============================================================================

# Comandos QEMU por arquitectura
QEMU_64_CMD = qemu-system-x86_64
QEMU_32_CMD = qemu-system-i386
QEMU_ARM64_CMD = qemu-system-aarch64
QEMU_ARM32_CMD = qemu-system-arm

# Configuración QEMU básica
QEMU_MEMORY = 512M
QEMU_FLAGS = -no-reboot -no-shutdown
QEMU_TIMEOUT = 30

# Modos de display
QEMU_DISPLAY_GTK = -display gtk
QEMU_DISPLAY_SDL = -display sdl2
QEMU_DISPLAY_NONE = -display none
QEMU_NGRAPHIC = -nographic

# Configuración por defecto
QEMU_DISPLAY = $(QEMU_DISPLAY_GTK)
QEMU_SERIAL = -serial stdio
QEMU_TEST_DISPLAY = $(QEMU_DISPLAY_NONE)  # Para pruebas automáticas

# Opciones de debugging y logging
QEMU_DEBUG_INT = -d int,cpu_reset
QEMU_DEBUG_EXEC = -d exec
QEMU_DEBUG_GUEST = -d guest_errors
QEMU_DEBUG_PAGE = -d page
QEMU_DEBUG_ALL = -d int,cpu_reset,exec,guest_errors,page
QEMU_LOG_FILE = -D qemu_debug.log

# Flags específicos por arquitectura
QEMU_64_FLAGS = -cdrom
QEMU_32_FLAGS = -cdrom
QEMU_ARM64_FLAGS = -M virt -cpu cortex-a57 -kernel
QEMU_ARM32_FLAGS = -M vexpress-a9 -cpu cortex-a9 -kernel
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

# Objetos base del kernel (comunes a todas las arquitecturas)
KERNEL_BASE_OBJS = includes/ir0/print.o \
                   includes/ir0/logging.o \
                   includes/ir0/validation.o \
                   includes/string.o \
                   interrupt/arch/idt.o \
                   interrupt/arch/pic.o \
                   interrupt/arch/isr_handlers.o \
                   interrupt/arch/keyboard.o \
                   includes/ir0/panic/panic.o \
                   drivers/timer/pit/pit.o \
drivers/timer/clock_system.o \
drivers/timer/best_clock.o \
drivers/timer/hpet/hpet.o \
drivers/timer/hpet/find_hpet.o \
drivers/timer/lapic/lapic.o \
drivers/timer/rtc/rtc.o \
                   drivers/IO/ps2.o \
                   drivers/storage/ata.o \
                   kernel/scheduler/priority_scheduler.o \
                   kernel/scheduler/round-robin_scheduler.o \
                   kernel/scheduler/sched_central.o \
                   kernel/scheduler/cfs_scheduler.o \
                   kernel/scheduler/scheduler_detection.o \
                   kernel/scheduler/task_impl.o \
                   kernel/process/process.o \
                   kernel/auth/auth.o \
                   kernel/login/login_system.o \
                   kernel/syscalls/syscalls.o \
                   kernel/shell/shell.o \
                   arch/common/arch_interface.o \
                   memory/bump_allocator.o \
                   memory/memory_manager.o \
                   memory/slab_allocator.o \
                   memory/buddy_allocator.o \
                   setup/kernel_config.o \
                   fs/minix_fs.o \
                   kernel/kernel_start.o \
                   examples/minix_test.o \
                   examples/minix_ata_test.o

# Objetos condicionales según build target
ifeq ($(BUILD_TARGET),desktop)
    CONDITIONAL_OBJS = 
else ifeq ($(BUILD_TARGET),server)
    CONDITIONAL_OBJS = 
else ifeq ($(BUILD_TARGET),iot)
    CONDITIONAL_OBJS = 
else ifeq ($(BUILD_TARGET),embedded)
    CONDITIONAL_OBJS = 
endif

# Objetos de arquitectura
ifeq ($(ARCH),x86-64)
    ARCH_OBJS = arch/x86-64/sources/arch_x64.o \
                arch/x86-64/asm/boot_x64.o \
                arch/x86-64/sources/idt_arch_x64.o \
                arch/x86-64/sources/fault.o \
                arch/x86-64/sources/tss_x64.o \
                kernel/scheduler/switch/switch_x64.o \
                interrupt/arch/x86-64/isr_stubs_64.o \
                memory/paging_x64.o
else ifeq ($(ARCH),x86-32)  
    ARCH_OBJS = arch/x86-32/sources/arch_x86.o \
                arch/x86-32/asm/boot_x86.o \
                kernel/scheduler/switch/switch_x86.o \
                interrupt/arch/x86-32/isr_stubs_32.o \
                memory/paging_x86-32.o
else ifeq ($(ARCH),arm64)
    ARCH_OBJS = arch/arm64/sources/arch_arm64.o \
                arch/arm64/asm/boot_arm64.o \
                arch/arm64/sources/exception_arm64.o \
                memory/arch/arm64/paging_arm64.o \
                memory/arch/arm64/mmu_arm64.o \
                kernel/scheduler/switch/switch_arm64.o \
                interrupt/arch/arm64/interrupt.o
else ifeq ($(ARCH),arm32)
    ARCH_OBJS = arch/arm-32/sources/arch_arm.o
endif

# Todos los objetos
ALL_OBJS = $(KERNEL_BASE_OBJS) $(CONDITIONAL_OBJS) $(ARCH_OBJS)

# Reglas de compilación para archivos .c
%.o: %.c
	@echo "Compilando $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Reglas de compilación para archivos .asm
%.o: %.asm
	@echo "Ensamblando $<..."
	$(ASM) $(ASMFLAGS) $< -o $@

# Compilar kernel para arquitectura específica
kernel-$(ARCH)-$(TARGET_NAME).bin: $(ALL_OBJS) $(ARCH_SUBDIRS)/linker.ld
	@echo "Enlazando kernel para $(ARCH)-$(TARGET_NAME)..."
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_BASE_OBJS) $(CONDITIONAL_OBJS) $(ARCH_OBJS)
	@echo "Kernel $(ARCH)-$(TARGET_NAME) compilado: $@"

# Crear ISO específico por arquitectura y target
kernel-$(ARCH)-$(TARGET_NAME).iso: kernel-$(ARCH)-$(TARGET_NAME).bin
	@echo "Creando ISO para $(ARCH)-$(TARGET_NAME)..."
	@mkdir -p iso-$(ARCH)-$(TARGET_NAME)/boot/grub
	@cp $(ARCH_SUBDIRS)/grub.cfg iso-$(ARCH)-$(TARGET_NAME)/boot/grub/
	@cp kernel-$(ARCH)-$(TARGET_NAME).bin iso-$(ARCH)-$(TARGET_NAME)/boot/
	@grub-mkrescue -o $@ iso-$(ARCH)-$(TARGET_NAME)
	@echo "ISO $(ARCH)-$(TARGET_NAME) creado: $@"

# Targets simplificados para compatibilidad
kernel-$(ARCH).bin: kernel-$(ARCH)-$(TARGET_NAME).bin
	@ln -sf $< $@

kernel-$(ARCH).iso: kernel-$(ARCH)-$(TARGET_NAME).iso
	@ln -sf $< $@

# Target por defecto
all: kernel-$(ARCH)-$(TARGET_NAME).iso

# Target para ejecutar en QEMU (por defecto con GUI)
run: run-gui



# Target para ejecutar en QEMU (Display mode - con interfaz gráfica)
run-display: kernel-$(ARCH)-$(TARGET_NAME).iso
	@echo "Ejecutando kernel $(ARCH)-$(TARGET_NAME) en QEMU (Display mode - con interfaz gráfica)..."
ifeq ($(ARCH),x86-64)
	$(QEMU_64_CMD) $(QEMU_64_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).iso -m $(QEMU_MEMORY) $(QEMU_FLAGS) $(QEMU_DISPLAY)
else ifeq ($(ARCH),x86-32)
	$(QEMU_32_CMD) $(QEMU_32_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).iso -m $(QEMU_MEMORY) $(QEMU_FLAGS) $(QEMU_DISPLAY)
else ifeq ($(ARCH),arm64)
	$(QEMU_ARM64_CMD) $(QEMU_ARM64_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).bin -m $(QEMU_MEMORY) $(QEMU_FLAGS) $(QEMU_DISPLAY)
else ifeq ($(ARCH),arm32)
	$(QEMU_ARM32_CMD) $(QEMU_ARM32_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).bin -m $(QEMU_MEMORY) $(QEMU_FLAGS) $(QEMU_DISPLAY)
endif

# Target para debug en QEMU
debug: kernel-$(ARCH)-$(TARGET_NAME).iso
	@echo "Ejecutando kernel $(ARCH)-$(TARGET_NAME) en QEMU con debug..."
ifeq ($(ARCH),x86-64)
	qemu-system-x86_64 -cdrom kernel-$(ARCH)-$(TARGET_NAME).iso -m 512M -no-reboot -no-shutdown -display gtk -d int,cpu_reset -D qemu_debug.log
else
	qemu-system-i386 -cdrom kernel-$(ARCH)-$(TARGET_NAME).iso -m 512M -no-reboot -no-shutdown -display gtk -d int,cpu_reset -D qemu_debug.log
endif

# Compilar para todas las arquitecturas
all-arch: 
	@echo "Compilando para todas las arquitecturas..."
	@for arch in x86-32 x86-64; do \
		echo "Compilando $$arch..."; \
		$(MAKE) ARCH=$$arch BUILD_TARGET=$(BUILD_TARGET) kernel-$$arch-$(TARGET_NAME).iso; \
	done

# Compilar para todos los build targets
all-targets:
	@echo "Compilando para todos los build targets..."
	@for target in desktop server iot embedded; do \
		echo "Compilando target $$target..."; \
		$(MAKE) ARCH=$(ARCH) BUILD_TARGET=$$target kernel-$(ARCH)-$$target.iso; \
	done

# Compilar todas las combinaciones
all-combinations:
	@echo "Compilando todas las combinaciones..."
	@for arch in x86-32 x86-64; do \
		for target in desktop server iot embedded; do \
			echo "Compilando $$arch-$$target..."; \
			$(MAKE) ARCH=$$arch BUILD_TARGET=$$target kernel-$$arch-$$target.iso; \
		done; \
	done

# Limpiar archivos de compilación
clean:
	@echo "Limpiando archivos de compilación..."
	@find . -name "*.o" -type f -delete
	@find . -name "*.d" -type f -delete
	@find . -name "*.bin" -type f -delete
	@find . -name "*.iso" -type f -delete
	@find . -name "*.elf" -type f -delete
	@find . -name "*.img" -type f -delete
	@find . -name "*.map" -type f -delete
	@find . -name "*.lst" -type f -delete
	@find . -name "*.tmp" -type f -delete
	@find . -name "*.log" -type f -delete
	@find . -name "qemu_*" -type f -delete
	@rm -rf iso-*
	@rm -rf tmp-*
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
	@echo "Build target: $(BUILD_TARGET)"
	@echo "Compilador: $(CC)"
	@echo "Ensamblador: $(ASM)"
	@echo "Linker: $(LD)"
	@echo "Flags C: $(CFLAGS)"
	@echo "Flags ASM: $(ASMFLAGS)"
	@echo "Flags LD: $(LDFLAGS)"

# ===============================================================================
# TARGETS DE PRUEBAS
# ===============================================================================

# Ejecutar todas las pruebas
test: test-compile test-qemu

# Pruebas de compilación
test-compile:
	@echo "Ejecutando pruebas de compilación..."
	@./scripts/test_framework.sh compile

# Pruebas de QEMU
test-qemu:
	@echo "Ejecutando pruebas de QEMU..."
	@./scripts/test_framework.sh qemu

# Pruebas de QEMU con display
test-qemu-display:
	@echo "Ejecutando pruebas de QEMU con display..."
	@./scripts/test_framework.sh qemu-display

# Prueba específica
test-specific:
	@echo "Ejecutando prueba específica: $(TEST_NAME)"
	@./scripts/test_framework.sh $(TEST_NAME)

# Ejecutar todas las pruebas del framework
test-all:
	@echo "Ejecutando suite completa de pruebas..."
	@./scripts/test_framework.sh all

# ===============================================================================
# INFORMACIÓN DE AYUDA
# ===============================================================================

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
	@echo "  make ARCH=x86-64        - Compilar para 64-bit"
	@echo "  make ARCH=x86-32        - Compilar para 32-bit"
	@echo "  make all-arch           - Compilar para todas las arquitecturas"
	@echo "  make all-targets        - Compilar para todos los build targets"
	@echo "  make all-combinations   - Compilar todas las combinaciones"
	@echo ""
	@echo "Comandos QEMU simples:"
	@echo "  make run                - Ejecutar kernel en QEMU con GUI"
	@echo "  make run-gui            - Ejecutar kernel en QEMU con GUI"
	@echo "  make run-nographic      - Ejecutar kernel en QEMU sin GUI (terminal)"
	@echo "  make run-test           - Ejecutar kernel en QEMU para testing"
	@echo "  make debug              - Ejecutar kernel con debug"
	@echo ""
	@echo "Comandos de limpieza:"
	@echo "  make clean              - Limpiar archivos de compilación"
	@echo "  make clean-all          - Limpiar todo"
	@echo ""
	@echo "Comandos de ayuda:"
	@echo "  make help               - Mostrar esta ayuda"
	@echo "  make help-qemu          - Mostrar ayuda específica de QEMU"

# ===============================================================================
# COMANDOS QEMU SIMPLES Y RÁPIDOS
# ===============================================================================

# Ejecutar con GUI (por defecto)
run-gui: all
	@echo "🚀 Ejecutando kernel $(ARCH)-$(TARGET_NAME) en QEMU con GUI..."
ifeq ($(ARCH),x86-64)
	$(QEMU_64_CMD) $(QEMU_64_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).iso -hda disk.img -m $(QEMU_MEMORY) $(QEMU_FLAGS) $(QEMU_DISPLAY_GTK)
else ifeq ($(ARCH),x86-32)
	$(QEMU_32_CMD) $(QEMU_32_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).iso -hda disk.img -m $(QEMU_MEMORY) $(QEMU_FLAGS) $(QEMU_DISPLAY_GTK)
else ifeq ($(ARCH),arm64)
	$(QEMU_ARM64_CMD) $(QEMU_ARM64_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).bin -hda disk.img -m $(QEMU_MEMORY) $(QEMU_FLAGS) $(QEMU_DISPLAY_GTK)
else ifeq ($(ARCH),arm32)
	$(QEMU_ARM32_CMD) $(QEMU_ARM32_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).bin -hda disk.img -m $(QEMU_MEMORY) $(QEMU_FLAGS) $(QEMU_DISPLAY_GTK)
endif

run-console: all
	@echo "🚀 Ejecutando kernel $(ARCH)-$(TARGET_NAME) en QEMU sin GUI..."
ifeq ($(ARCH),x86-64)
	$(QEMU_64_CMD) $(QEMU_64_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).iso -hda disk.img -m $(QEMU_MEMORY) $(QEMU_FLAGS) -nographic
else ifeq ($(ARCH),x86-32)
	$(QEMU_32_CMD) $(QEMU_32_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).iso -hda disk.img -m $(QEMU_MEMORY) $(QEMU_FLAGS) -nographic
else ifeq ($(ARCH),arm64)
	$(QEMU_ARM64_CMD) $(QEMU_ARM64_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).bin -hda disk.img -m $(QEMU_MEMORY) $(QEMU_FLAGS) -nographic
else ifeq ($(ARCH),arm32)
	$(QEMU_ARM32_CMD) $(QEMU_ARM32_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).bin -hda disk.img -m $(QEMU_MEMORY) $(QEMU_FLAGS) -nographic
endif

# Ejecutar sin GUI (terminal)
run-nographic: all
	@echo "🖥️  Ejecutando kernel $(ARCH)-$(TARGET_NAME) en QEMU sin GUI (terminal)..."
ifeq ($(ARCH),x86-64)
	$(QEMU_64_CMD) $(QEMU_64_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).iso -m $(QEMU_MEMORY) $(QEMU_FLAGS) $(QEMU_NGRAPHIC) $(QEMU_SERIAL)
else ifeq ($(ARCH),x86-32)
	$(QEMU_32_CMD) $(QEMU_32_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).iso -m $(QEMU_MEMORY) $(QEMU_FLAGS) $(QEMU_NGRAPHIC) $(QEMU_SERIAL)
else ifeq ($(ARCH),arm64)
	$(QEMU_ARM64_CMD) $(QEMU_ARM64_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).bin -m $(QEMU_MEMORY) $(QEMU_FLAGS) $(QEMU_NGRAPHIC) $(QEMU_SERIAL)
else ifeq ($(ARCH),arm32)
	$(QEMU_ARM32_CMD) $(QEMU_ARM32_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).bin -m $(QEMU_MEMORY) $(QEMU_FLAGS) $(QEMU_NGRAPHIC) $(QEMU_SERIAL)
endif

# Ejecutar para testing (sin display, con timeout)
run-test: all
	@echo "🧪 Ejecutando kernel $(ARCH)-$(TARGET_NAME) en QEMU para testing..."
ifeq ($(ARCH),x86-64)
	timeout $(QEMU_TIMEOUT) $(QEMU_64_CMD) $(QEMU_64_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).iso -m $(QEMU_MEMORY) $(QEMU_FLAGS) $(QEMU_SERIAL) $(QEMU_DISPLAY_NONE) || true
else ifeq ($(ARCH),x86-32)
	timeout $(QEMU_TIMEOUT) $(QEMU_32_CMD) $(QEMU_32_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).iso -m $(QEMU_MEMORY) $(QEMU_FLAGS) $(QEMU_SERIAL) $(QEMU_DISPLAY_NONE) || true
else ifeq ($(ARCH),arm64)
	timeout $(QEMU_TIMEOUT) $(QEMU_ARM64_CMD) $(QEMU_ARM64_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).bin -m $(QEMU_MEMORY) $(QEMU_FLAGS) $(QEMU_SERIAL) $(QEMU_DISPLAY_NONE) || true
else ifeq ($(ARCH),arm32)
	timeout $(QEMU_TIMEOUT) $(QEMU_ARM32_CMD) $(QEMU_ARM32_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).bin -m $(QEMU_MEMORY) $(QEMU_FLAGS) $(QEMU_SERIAL) $(QEMU_DISPLAY_NONE) || true
endif

# Comandos rápidos para 32-bit y 64-bit
run-32: 
	@$(MAKE) ARCH=x86-32 run-gui

run-64:
	@$(MAKE) ARCH=x86-64 run-gui

run-32-nographic:
	@$(MAKE) ARCH=x86-32 run-nographic

run-64-nographic:
	@$(MAKE) ARCH=x86-64 run-nographic

# Comandos con debugging
run-debug: all
	@echo "🐛 Ejecutando kernel $(ARCH)-$(TARGET_NAME) en QEMU con debugging..."
ifeq ($(ARCH),x86-64)
	$(QEMU_64_CMD) $(QEMU_64_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).iso -m $(QEMU_MEMORY) $(QEMU_FLAGS) $(QEMU_DISPLAY_GTK) $(QEMU_DEBUG_ALL) $(QEMU_LOG_FILE)
else ifeq ($(ARCH),x86-32)
	$(QEMU_32_CMD) $(QEMU_32_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).iso -m $(QEMU_MEMORY) $(QEMU_FLAGS) $(QEMU_DISPLAY_GTK) $(QEMU_DEBUG_ALL) $(QEMU_LOG_FILE)
else ifeq ($(ARCH),arm64)
	$(QEMU_ARM64_CMD) $(QEMU_ARM64_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).bin -m $(QEMU_MEMORY) $(QEMU_FLAGS) $(QEMU_DISPLAY_GTK) $(QEMU_DEBUG_ALL) $(QEMU_LOG_FILE)
else ifeq ($(ARCH),arm32)
	$(QEMU_ARM32_CMD) $(QEMU_ARM32_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).bin -m $(QEMU_MEMORY) $(QEMU_FLAGS) $(QEMU_DISPLAY_GTK) $(QEMU_DEBUG_ALL) $(QEMU_LOG_FILE)
endif

run-debug-nographic: all
	@echo "🐛 Ejecutando kernel $(ARCH)-$(TARGET_NAME) en QEMU con debugging (terminal)..."
ifeq ($(ARCH),x86-64)
	$(QEMU_64_CMD) $(QEMU_64_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).iso -m $(QEMU_MEMORY) $(QEMU_FLAGS) $(QEMU_NGRAPHIC) $(QEMU_DEBUG_ALL) $(QEMU_LOG_FILE)
else ifeq ($(ARCH),x86-32)
	$(QEMU_32_CMD) $(QEMU_32_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).iso -m $(QEMU_MEMORY) $(QEMU_FLAGS) $(QEMU_NGRAPHIC) $(QEMU_DEBUG_ALL) $(QEMU_LOG_FILE)
else ifeq ($(ARCH),arm64)
	$(QEMU_ARM64_CMD) $(QEMU_ARM64_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).bin -m $(QEMU_MEMORY) $(QEMU_FLAGS) $(QEMU_NGRAPHIC) $(QEMU_DEBUG_ALL) $(QEMU_LOG_FILE)
else ifeq ($(ARCH),arm32)
	$(QEMU_ARM32_CMD) $(QEMU_ARM32_FLAGS) kernel-$(ARCH)-$(TARGET_NAME).bin -m $(QEMU_MEMORY) $(QEMU_FLAGS) $(QEMU_NGRAPHIC) $(QEMU_DEBUG_ALL) $(QEMU_LOG_FILE)
endif

# Comandos rápidos con debugging
run-32-debug:
	@$(MAKE) ARCH=x86-32 run-debug

run-64-debug:
	@$(MAKE) ARCH=x86-64 run-debug

run-32-debug-nographic:
	@$(MAKE) ARCH=x86-32 run-debug-nographic

run-64-debug-nographic:
	@$(MAKE) ARCH=x86-64 run-debug-nographic

# Ayuda específica de QEMU
help-qemu:
	@echo "🎮 IR0 Kernel QEMU Commands"
	@echo ""
	@echo "Comandos principales:"
	@echo "  make run-gui              - Ejecutar con interfaz gráfica (GTK)"
	@echo "  make run-nographic        - Ejecutar en terminal (sin GUI)"
	@echo "  make run-test             - Ejecutar para testing (con timeout)"
	@echo ""
	@echo "Comandos con debugging:"
	@echo "  make run-debug            - Ejecutar con debugging completo"
	@echo "  make run-debug-nographic  - Ejecutar con debugging en terminal"
	@echo ""
	@echo "Comandos rápidos por arquitectura:"
	@echo "  make run-32               - Compilar y ejecutar 32-bit con GUI"
	@echo "  make run-64               - Compilar y ejecutar 64-bit con GUI"
	@echo "  make run-32-nographic     - Compilar y ejecutar 32-bit sin GUI"
	@echo "  make run-64-nographic     - Compilar y ejecutar 64-bit sin GUI"
	@echo "  make run-32-debug         - Compilar y ejecutar 32-bit con debugging"
	@echo "  make run-64-debug         - Compilar y ejecutar 64-bit con debugging"
	@echo ""
	@echo "Opciones de QEMU incluidas:"
	@echo "  -no-reboot               - No reiniciar automáticamente"
	@echo "  -no-shutdown             - No apagar automáticamente"
	@echo "  -nographic               - Modo terminal (sin GUI)"
	@echo "  -display gtk             - Interfaz gráfica GTK"
	@echo "  -serial stdio            - Salida serial a terminal"
	@echo ""
	@echo "Opciones de debugging:"
	@echo "  -d int,cpu_reset         - Log de interrupciones y resets"
	@echo "  -d exec                  - Log de ejecución de instrucciones"
	@echo "  -d guest_errors          - Log de errores del guest"
	@echo "  -d page                  - Log de page faults"
	@echo "  -D qemu_debug.log        - Archivo de log de debugging"
	@echo ""
	@echo "Para salir de QEMU:"
	@echo "  Ctrl+A, X                - Salir de QEMU"
	@echo "  Ctrl+C                   - Interrumpir ejecución"

.PHONY: all all-arch all-targets all-combinations run debug clean clean-all arch-details help run-gui run-nographic run-test run-32 run-64 run-32-nographic run-64-nographic run-debug run-debug-nographic run-32-debug run-64-debug run-32-debug-nographic run-64-debug-nographic help-qemu

