# ===============================================================================
# IR0 KERNEL MAKEFILE - x86-64 ONLY
# ===============================================================================
KERNEL_ROOT := $(CURDIR)

# Architecture: x86-64 only
ARCH := x86-64

# Build target
BUILD_TARGET := desktop
CFLAGS_TARGET := -DIR0_DESKTOP

# Información de versión del kernel
IR0_VERSION_MAJOR := 1
IR0_VERSION_MINOR := 0
IR0_VERSION_PATCH := 0
IR0_VERSION_STRING := $(IR0_VERSION_MAJOR).$(IR0_VERSION_MINOR).$(IR0_VERSION_PATCH)
IR0_BUILD_DATE := $(shell date +%Y-%m-%d)
IR0_BUILD_TIME := $(shell date +%H:%M:%S)

# ===============================================================================
# COMPILER CONFIGURATION (x86-64)
# ===============================================================================
CC = gcc
ASM = nasm  
LD = ld

# Compiler flags
CFLAGS = -m64 -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2
CFLAGS += -D__x86_64__
CFLAGS += -nostdlib -nostdinc -fno-builtin -fno-stack-protector
CFLAGS += -fno-pic -nodefaultlibs -ffreestanding
CFLAGS += -Wall -Wextra -Werror=implicit-function-declaration -O0 -g -MMD -MP
CFLAGS += $(CFLAGS_TARGET)

# Include paths
CFLAGS += -I$(KERNEL_ROOT)
CFLAGS += -I$(KERNEL_ROOT)/includes
CFLAGS += -I$(KERNEL_ROOT)/includes/ir0
CFLAGS += -I$(KERNEL_ROOT)/arch/common
CFLAGS += -I$(KERNEL_ROOT)/arch/x86-64/include
CFLAGS += -I$(KERNEL_ROOT)/arch/x86-64/sources
CFLAGS += -I$(KERNEL_ROOT)/setup
CFLAGS += -I$(KERNEL_ROOT)/memory
CFLAGS += -I$(KERNEL_ROOT)/interrupt
CFLAGS += -I$(KERNEL_ROOT)/drivers
CFLAGS += -I$(KERNEL_ROOT)/fs
CFLAGS += -I$(KERNEL_ROOT)/kernel

# Assembler and linker flags
ASMFLAGS = -f elf64
LDFLAGS = -m elf_x86_64 -T arch/x86-64/linker.ld

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

# ===============================================================================
# KERNEL OBJECTS (CONSOLIDATED)
# ===============================================================================
KERNEL_OBJS = \
    kernel/kernel.o \
    kernel/init.o \
    kernel/process.o \
    kernel/scheduler.o \
    kernel/task.o \
    kernel/syscalls.o \
    kernel/shell.o

MEMORY_OBJS = \
    memory/allocator.o \
    memory/paging.o

LIB_OBJS = \
    includes/ir0/print.o \
    includes/ir0/logging.o \
    includes/ir0/validation.o \
    includes/ir0/panic/panic.o \
    includes/string.o

INTERRUPT_OBJS = \
    interrupt/arch/idt.o \
    interrupt/arch/pic.o \
    interrupt/arch/isr_handlers.o \
    interrupt/arch/keyboard.o \
    interrupt/arch/x86-64/isr_stubs_64.o

DRIVER_OBJS = \
    drivers/IO/ps2.o \
    drivers/timer/pit/pit.o \
    drivers/timer/clock_system.o \
    drivers/timer/best_clock.o \
    drivers/timer/rtc/rtc.o \
    drivers/timer/hpet/hpet.o \
    drivers/timer/hpet/find_hpet.o \
    drivers/timer/lapic/lapic.o \
    drivers/storage/ata.o \
    drivers/video/vbe.o

FS_OBJS = \
    fs/minix_fs.o \
    fs/vfs_simple.o

ARCH_OBJS = \
    arch/x86-64/sources/arch_x64.o \
    arch/x86-64/sources/gdt.o \
    arch/x86-64/sources/tss_x64.o \
    arch/x86-64/sources/user_mode.o \
    arch/x86-64/sources/idt_arch_x64.o \
    arch/x86-64/sources/fault.o \
    arch/x86-64/asm/boot_x64.o \
    arch/x86-64/asm/syscall_entry_64.o \
    arch/common/arch_interface.o \
    kernel/scheduler/switch/switch_x64.o

SETUP_OBJS = \
    setup/kernel_config.o

# All objects
ALL_OBJS = $(KERNEL_OBJS) $(MEMORY_OBJS) $(LIB_OBJS) $(INTERRUPT_OBJS) \
           $(DRIVER_OBJS) $(FS_OBJS) $(ARCH_OBJS) $(SETUP_OBJS)

# ===============================================================================
# BUILD RULES
# ===============================================================================

# Compile C files
%.o: %.c
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile ASM files
%.o: %.asm
	@echo "  ASM     $<"
	@$(ASM) $(ASMFLAGS) $< -o $@

# Link kernel
kernel-x64.bin: $(ALL_OBJS) arch/x86-64/linker.ld
	@echo "  LD      $@"
	@$(LD) $(LDFLAGS) -o $@ $(ALL_OBJS)
	@echo "✓ Kernel linked: $@"

# Create ISO
kernel-x64.iso: kernel-x64.bin
	@echo "  ISO     $@"
	@mkdir -p iso/boot/grub
	@cp arch/x86-64/grub.cfg iso/boot/grub/
	@cp kernel-x64.bin iso/boot/
	@grub-mkrescue -o $@ iso 2>/dev/null
	@echo "✓ ISO created: $@"

# Default target
all: kernel-x64.iso

# ===============================================================================
# QEMU COMMANDS
# ===============================================================================

# Run with GUI and disk (default)
run: kernel-x64.iso
	@echo "🚀 Running IR0 Kernel..."
	qemu-system-x86_64 -cdrom kernel-x64.iso \
	    -drive file=disk.img,format=raw,if=ide,index=0 \
	    -m 512M -no-reboot -no-shutdown \
	    -display gtk -serial stdio \
	    -d guest_errors -D qemu_debug.log

# Run without disk
run-nodisk: kernel-x64.iso
	@echo "🚀 Running IR0 Kernel (no disk)..."
	qemu-system-x86_64 -cdrom kernel-x64.iso \
	    -m 512M -no-reboot -no-shutdown \
	    -display gtk -serial stdio

# Run in console mode
run-console: kernel-x64.iso
	@echo "🚀 Running IR0 Kernel (console)..."
	qemu-system-x86_64 -cdrom kernel-x64.iso \
	    -drive file=disk.img,format=raw,if=ide,index=0 \
	    -m 512M -no-reboot -no-shutdown \
	    -nographic

# Debug mode
debug: kernel-x64.iso
	@echo "🐛 Running IR0 Kernel (debug)..."
	qemu-system-x86_64 -cdrom kernel-x64.iso \
	    -drive file=disk.img,format=raw,if=ide,index=0 \
	    -m 512M -no-reboot -no-shutdown \
	    -display gtk -serial stdio \
	    -d int,cpu_reset,guest_errors -D qemu_debug.log

# Create disk image
create-disk:
	@echo "🔧 Creating virtual disk..."
	@./scripts/create_disk.sh

# ===============================================================================
# CLEAN
# ===============================================================================

clean:
	@echo "🧹 Cleaning build artifacts..."
	@find . -name "*.o" -type f -delete
	@find . -name "*.d" -type f -delete
	@find . -name "*.bin" -type f -delete
	@find . -name "*.iso" -type f -delete
	@rm -rf iso/
	@rm -f qemu_debug.log
	@echo "✓ Clean complete"

# ===============================================================================
# HELP
# ===============================================================================

help:
	@echo "╔════════════════════════════════════════════════════════════╗"
	@echo "║       IR0 KERNEL - BUILD SYSTEM (x86-64 ONLY)             ║"
	@echo "╚════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "📦 Build:"
	@echo "  make all              Build kernel ISO"
	@echo "  make clean            Clean build artifacts"
	@echo ""
	@echo "🚀 Run:"
	@echo "  make run              Run with GUI + disk (recommended)"
	@echo "  make run-nodisk       Run without disk"
	@echo "  make run-console      Run in console mode"
	@echo "  make debug            Run with full debug logging"
	@echo ""
	@echo "🔧 Utilities:"
	@echo "  make create-disk      Create virtual disk for MINIX FS"
	@echo "  make help             Show this help"
	@echo ""
	@echo "💡 Quick start: make run"
	@echo ""

# ===============================================================================
# PHONY TARGETS
# ===============================================================================

.PHONY: all clean run run-nodisk run-console debug create-disk help

# Include dependency files
-include $(ALL_OBJS:.o=.d)
