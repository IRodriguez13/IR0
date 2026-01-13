# IR0 KERNEL MAKEFILE - x86-64 ONLY

KERNEL_ROOT := $(CURDIR)

# Default architecture (can be overridden with arch= parameter)
# Supported: x86-64, x86-32, arm64, arm32
ARCH ?= x86-64

# Build target
BUILD_TARGET := desktop
CFLAGS_TARGET := -DIR0_DESKTOP


IR0_VERSION_MAJOR := 0
IR0_VERSION_MINOR := 0
IR0_VERSION_PATCH := 1
IR0_VERSION_SUFFIX := -pre-rc3
IR0_VERSION_STRING := $(IR0_VERSION_MAJOR).$(IR0_VERSION_MINOR).$(IR0_VERSION_PATCH)$(IR0_VERSION_SUFFIX)

# Build information 

IR0_BUILD_DATE := $(shell LC_TIME=C date +"%b %d %Y")
IR0_BUILD_TIME := $(shell date +"%H:%M:%S")
IR0_BUILD_USER := $(shell whoami 2>/dev/null || echo "unknown")
IR0_BUILD_HOST := $(shell hostname 2>/dev/null || echo "localhost")
IR0_BUILD_CC := $(shell $(CC) --version 2>/dev/null | head -n1 | cut -d' ' -f1-3 || echo "gcc unknown")
# Build number - auto-increment on each build
IR0_BUILD_NUMBER := $(shell if [ -f .build_number ]; then \
	cat .build_number; \
else \
	echo "1" > .build_number && echo "1"; \
fi)
# Increment build number for next build (only if building kernel target)
-include .build_number_inc

# COMPILER CONFIGURATION (x86-64)

# Build tools
CC = gcc
LD = ld
ASM = nasm
NASM = nasm
QEMU = qemu-system-x86_64
PYTHON = python3

# Flags
CFLAGS = -m64 -ffreestanding -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -nostdlib -lgcc -I./includes -I./ -g -Wall -Wextra -fno-stack-protector -fno-builtin
LDFLAGS = -T kernel/linker.ld -z max-page-size=0x1000
NASMFLAGS = -f elf64

# Directories
BUILD_DIR = build
ISO_DIR = iso

# Targets
.PHONY: all clean run debug menuconfig

all: $(BUILD_DIR)/kernel.iso

menuconfig:
	@echo "Building kconfig library..."
	@$(MAKE) -C setup -f Makefile.kconfig install || echo "Warning: Library build failed, GUI will use fallback"
	@echo "Launching IR0 Kernel Configuration Menu..."
	@$(MAKE) -C setup menuconfig
CFLAGS += $(CFLAGS_TARGET)

# Include paths
CFLAGS += -I$(KERNEL_ROOT)
CFLAGS += -I$(KERNEL_ROOT)/includes
CFLAGS += -I$(KERNEL_ROOT)/includes/ir0
CFLAGS += -I$(KERNEL_ROOT)/arch/common
CFLAGS += -I$(KERNEL_ROOT)/arch/$(ARCH)/include
CFLAGS += -I$(KERNEL_ROOT)/include
CFLAGS += -I$(KERNEL_ROOT)/kernel
CFLAGS += -I$(KERNEL_ROOT)/drivers
CFLAGS += -I$(KERNEL_ROOT)/fs
CFLAGS += -I$(KERNEL_ROOT)/interrupt
CFLAGS += -I$(KERNEL_ROOT)/memory
CFLAGS += -I$(KERNEL_ROOT)/scheduler
CFLAGS += -I$(KERNEL_ROOT)/includesnel

# Assembler and linker flags
ASMFLAGS = -f elf64
LDFLAGS = -m elf_x86_64 -T arch/x86-64/linker.ld

# Subsistemas comunes (siempre presentes)
COMMON_SUBDIRS = kernel interrupt drivers/timer drivers/IO drivers/storage kernel/scheduler includes includes/ir0 includes/ir0/panic arch/common memory setup net

# Subsistemas condicionales según build target

# CONFIGURACIÓN QEMU (ABSTRACCIÓN)

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

# Hardware soportado por IR0 Kernel (configuración completa)
# Network: RTL8139 y e1000 (Intel)
QEMU_NET_RTL8139 = -netdev user,id=net0 -device rtl8139,netdev=net0
# QEMU_NET_E1000 = -netdev user,id=net1 -device e1000,netdev=net1
QEMU_NET_ALL = $(QEMU_NET_RTL8139) $(QEMU_NET_E1000)

# Audio: Sound Blaster 16 y Adlib OPL2 (sintaxis moderna QEMU)
QEMU_AUDIO_SB16 = -device sb16
QEMU_AUDIO_ADLIB = -device adlib
QEMU_AUDIO_ALL = $(QEMU_AUDIO_SB16) $(QEMU_AUDIO_ADLIB)

# Storage: ATA/IDE disk
QEMU_STORAGE_IDE = -drive file=disk.img,format=raw,if=ide,index=0

# Serial: COM1 para debug
QEMU_SERIAL_COM1 = -serial stdio


QEMU_HW_IR0_ALL = $(QEMU_NET_ALL) $(QEMU_AUDIO_ALL) $(QEMU_STORAGE_IDE) $(QEMU_SERIAL_COM1)

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

# KERNEL OBJECTS 

KERNEL_OBJS = \
	kernel/main.o \
    kernel/init.o \
    kernel/process.o \
    kernel/task.o \
    kernel/syscalls.o \
    kernel/dbgshell.o \
    kernel/elf_loader.o \
    kernel/driver_registry.o

# Scheduler - Select which scheduler to compile
# Uncomment the scheduler you want to use and comment out the others
KERNEL_OBJS += kernel/rr_sched.o
# KERNEL_OBJS += kernel/cfs_sched.o
# KERNEL_OBJS += kernel/priority_sched.o

MEMORY_OBJS = \
	includes/ir0/memory/allocator.o \
	includes/ir0/memory/paging.o \
	includes/ir0/memory/pmm.o \
	includes/ir0/memory/kmem.o

LIB_OBJS = \
    includes/ir0/vga.o \
    includes/ir0/logging.o \
    includes/ir0/oops.o \
    includes/ir0/signals.o \
    includes/ir0/pipe.o \
    includes/ir0/copy_user.o \
    includes/string.o

INTERRUPT_OBJS = \
    interrupt/arch/idt.o \
    interrupt/arch/pic.o \
    interrupt/arch/isr_handlers.o \
    interrupt/arch/keyboard.o \
    interrupt/arch/x86-64/isr_stubs_64.o

DRIVER_OBJS = \
    drivers/IO/ps2.o \
    drivers/IO/ps2_mouse.o \
    drivers/IO/pc_speaker.o \
    drivers/audio/sound_blaster.o \
    drivers/audio/adlib.o \
    drivers/dma/dma.o \
    drivers/net/rtl8139.o \
    drivers/net/e1000.o \
    drivers/serial/serial.o \
    drivers/timer/pit/pit.o \
    drivers/timer/clock_system.o \
    drivers/timer/rtc/rtc.o \
    drivers/timer/hpet/hpet.o \
    drivers/timer/hpet/find_hpet.o \
    drivers/timer/lapic/lapic.o \
    drivers/storage/ata.o \
    drivers/storage/ata_helpers.o \
    drivers/storage/fs_types.o \
	drivers/video/vbe.o \
	drivers/video/typewriter.o \
	drivers/init_drv.o \
	$(MULTILANG_DRIVER_SUPPORT_OBJ)

FS_OBJS = \
    fs/vfs.o \
    fs/minix_fs.o \
    fs/vfs_simple.o \
    fs/path.o \
    fs/chmod.o \
    fs/ramfs.o \
    fs/permissions.o \
    fs/procfs.o \
    fs/devfs.o

DISK_OBJS = \
    drivers/disk/partition.o

NET_OBJS = \
    net/net.o \
    net/arp.o \
    net/ip.o \
    net/icmp.o

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

SETUP_OBJS =

CPP_OBJS = \
	cpp/runtime/compat.o

# Multi-language driver objects (Rust and C++) - Optional, only included if enabled
RUST_DRIVER_OBJS = 
CPP_DRIVER_OBJS = 
MULTILANG_DRIVER_SUPPORT_OBJ = 

# Include example drivers only if KERNEL_ENABLE_EXAMPLE_DRIVERS is enabled
# These are test/example drivers and should not be compiled by default
-include .example_drivers_enabled
ifeq ($(ENABLE_EXAMPLE_DRIVERS),1)
RUST_DRIVER_OBJS += rust/drivers/rust_simple_driver.o
CPP_DRIVER_OBJS += cpp/examples/cpp_example.o
MULTILANG_DRIVER_SUPPORT_OBJ += drivers/multilang_drivers.o
# Pass flag to compiler to enable driver registration in code
CFLAGS += -DKERNEL_ENABLE_EXAMPLE_DRIVERS=1
endif

# All objects
ALL_OBJS = $(KERNEL_OBJS) $(MEMORY_OBJS) $(LIB_OBJS) $(INTERRUPT_OBJS) \
           $(DRIVER_OBJS) $(FS_OBJS) $(ARCH_OBJS) $(SETUP_OBJS) $(DISK_OBJS) \
           $(CPP_OBJS) $(CPP_DRIVER_OBJS) $(RUST_DRIVER_OBJS) $(NET_OBJS)

# BUILD RULES

# Compile C files
%.o: %.c
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) \
		-DIR0_BUILD_DATE_STRING="\"$(IR0_BUILD_DATE)\"" \
		-DIR0_BUILD_TIME_STRING="\"$(IR0_BUILD_TIME)\"" \
		-DIR0_BUILD_USER_STRING="\"$(IR0_BUILD_USER)\"" \
		-DIR0_BUILD_HOST_STRING="\"$(IR0_BUILD_HOST)\"" \
		-DIR0_BUILD_CC_STRING="\"$(IR0_BUILD_CC)\"" \
		-DIR0_BUILD_NUMBER_STRING="\"$(IR0_BUILD_NUMBER)\"" \
		-c $< -o $@

# Compile C++ files
%.o: %.cpp
	@echo "  CXX     $<"
	@g++ -m64 -ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics \
		-mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
		-nostdlib -lgcc -g -Wall -Wextra -fno-stack-protector -fno-builtin \
		-I./cpp/include $(CFLAGS) -c $< -o $@

# Compile Rust files (using unibuild script)
%.o: %.rs
	@echo "  RUST    $<"
	@$(KERNEL_ROOT)/scripts/unibuild.sh -rust $<

# Compile ASM files
%.o: %.asm
	@echo "  ASM     $<"
	@$(ASM) $(ASMFLAGS) $< -o $@

# Flexible parameter system
ifeq ($(MAKECMDGOALS),auto)
    PARALLEL_JOBS := $(shell nproc)
    MAKEOVERRIDES := $(filter-out auto,$(MAKEOVERRIDES))
    .NOTPARALLEL:
endif

ifdef arch
    ifeq ($(arch),x86-64)
        ARCH := x86-64
        # x86-64 specific flags
    else ifeq ($(arch),x86-32)
        ARCH := x86-32
        # x86-32 specific flags
    else ifeq ($(arch),arm64)
        ARCH := arm64
        # ARM64 specific flags
    else ifeq ($(arch),arm32)
        ARCH := arm32
        # ARM32 specific flags
    else
        $(error Unsupported architecture: $(arch). Supported: x86-64, x86-32, arm64, arm32)
    endif
    CFLAGS += -DARCH_$(shell echo $(ARCH) | tr 'a-z' 'A-Z')
endif

# Link kernel
kernel-x64.bin: $(ALL_OBJS) arch/x86-64/linker.ld
	@echo "  LD      $@"
	@$(LD) $(LDFLAGS) -o $@ $(ALL_OBJS)
	@echo "✓ Kernel linked: $@"
	@echo "  BUILD   Incrementing build number..."
	@if [ -f .build_number ]; then \
		BUILD_NUM=$$(cat .build_number); \
		BUILD_NUM=$$((BUILD_NUM + 1)); \
		echo $$BUILD_NUM > .build_number; \
	else \
		echo "1" > .build_number; \
	fi

# Create ISO
kernel-x64.iso: kernel-x64.bin
	@echo "  ISO     $@"
	@rm -rf iso
	@mkdir -p iso/boot/grub
	@cp arch/x86-64/grub.cfg iso/boot/grub/
	@cp kernel-x64.bin iso/boot/
	@echo "Running grub-mkrescue (will print errors if it fails)..."
	@grub-mkrescue -o $@ iso
	@echo "✓ ISO created: $@"

# Create a raw disk image used by QEMU. If you want a persistent filesystem
# you can format and populate this image separately. This target will
# create a raw disk image if it doesn't already exist.
# Supports filesystem selection via FS=minix|fat32|ext4 and size via SIZE=MB (legacy)
# For new usage, use: make create-disk [filesystem] [size]
disk.img:
	@if [ -f $@ ]; then \
		echo "  DISK    $@ already exists"; \
	else \
		echo "  DISK    creating $@ using scripts/create_disk.sh"; \
		if [ -n "$(FS)" ]; then \
			if [ -n "$(SIZE)" ]; then \
				./scripts/create_disk.sh $(FS) $(SIZE) --output $@; \
			else \
				./scripts/create_disk.sh $(FS) --output $@; \
			fi; \
		else \
			./scripts/create_disk.sh --output $@; \
		fi; \
	fi
	@echo "✓ Disk image ready: $@"

# ============================================
# BUILD CONFIGURATION
# ============================================

# Default values
PARALLEL_JOBS ?= 1
# ARCH is already set at the top of the file

# Enable parallel builds if auto is specified
ifneq (,$(filter auto,$(MAKECMDGOALS)))
    PARALLEL_JOBS := $(shell nproc)
    MAKEOVERRIDES := $(filter-out auto,$(MAKEOVERRIDES))
    .NOTPARALLEL:
endif


# Process architecture parameter (if specified)
ifdef arch
    ifeq ($(filter $(arch),x86-64 x86-32 arm64 arm32),)
        $(error Unsupported architecture: $(arch). Supported: x86-64, x86-32, arm64, arm32)
    endif
    ARCH := $(arch)
    CFLAGS += -DARCH_$(shell echo $(ARCH) | tr 'a-z' 'A-Z')
else
    # Default to x86-64 if not specified
    ARCH := x86-64
    CFLAGS += -DARCH_X86_64
endif

# ============================================
# BUILD TARGETS
# ============================================

# Default target
ir0: kernel-x64.iso

# Build using all available CPU cores
ir0-auto: auto
	@# This is now just a compatibility alias

# Auto build target (used by ir0-auto for compatibility)
.PHONY: auto
auto: ir0

# Windows build target
windows win:
	@echo "Building IR0 Kernel for Windows..."
	@$(MAKE) -f Makefile.win ir0

windows-clean win-clean:
	@echo "Cleaning Windows build artifacts..."
	@$(MAKE) -f Makefile.win clean

# QEMU COMMANDS

# Run with GUI and disk (default) - ALL IR0 SUPPORTED HARDWARE
run: kernel-x64.iso disk.img
	@echo "🚀 Running IR0 Kernel with all supported hardware..."
	@echo "   Hardware: RTL8139, SB16, Adlib, ATA/IDE, Serial, PS/2, VGA"
	qemu-system-x86_64 -cdrom kernel-x64.iso \
		$(QEMU_HW_IR0_ALL) \
		-m 512M -no-reboot -no-shutdown \
		$(QEMU_DISPLAY) \
		$(QEMU_DEBUG_GUEST) $(QEMU_LOG_FILE)

# Run with GUI and serial debug output - ALL IR0 SUPPORTED HARDWARE
run-debug: kernel-x64.iso disk.img
	@echo "Running IR0 Kernel with debug output and all supported hardware..."
	@echo "   Hardware: RTL8139, e1000, SB16, ATA/IDE, Serial, PS/2, VGA"
	@echo "Serial output will appear in this terminal"
	@echo "QEMU GUI will open in separate window"
	@echo "Press Ctrl+C to stop"
	qemu-system-x86_64 -cdrom kernel-x64.iso \
		$(QEMU_HW_IR0_ALL) \
		-m 512M -no-reboot -no-shutdown \
		$(QEMU_DISPLAY) \
		-monitor telnet:127.0.0.1:1234,server,nowait \
		-d guest_errors,int $(QEMU_LOG_FILE)

# Run without disk
run-nodisk: kernel-x64.iso
	@echo "Running IR0 Kernel (no disk)..."
	qemu-system-x86_64 -cdrom kernel-x64.iso \
		-m 512M -no-reboot -no-shutdown \
		-display gtk -serial stdio

# Run in console mode (attach disk image) - ALL IR0 SUPPORTED HARDWARE
run-console: kernel-x64.iso disk.img
	@echo "Running IR0 Kernel (console) with all supported hardware..."
	@echo "   Hardware: RTL8139, e1000, SB16, ATA/IDE, Serial, PS/2"
	qemu-system-x86_64 -cdrom kernel-x64.iso \
		$(QEMU_HW_IR0_ALL) \
		-m 512M -no-reboot -no-shutdown \
		$(QEMU_NGRAPHIC)

# Debug mode (detailed QEMU logging) - ALL IR0 SUPPORTED HARDWARE
debug: kernel-x64.iso disk.img
	@echo "Running IR0 Kernel (debug) with all supported hardware..."
	@echo "   Hardware: RTL8139, e1000, SB16, ATA/IDE, Serial, PS/2, VGA"
	qemu-system-x86_64 -cdrom kernel-x64.iso \
		$(QEMU_HW_IR0_ALL) \
		-m 512M -no-reboot -no-shutdown \
		$(QEMU_DISPLAY) \
		-d int,cpu_reset,guest_errors $(QEMU_LOG_FILE)

# Create disk image (wrapper for scripts/create_disk.sh)
# Usage: make create-disk [filesystem] [size]
# Examples:
#   make create-disk              # Create 200MB MINIX disk (default)
#   make create-disk minix 500    # Create 500MB MINIX disk
#   make create-disk fat32        # Create FAT32 disk (default size)
#   make create-disk fat32 500    # Create 500MB FAT32 disk
#   make create-disk ext4 1000    # Create 1GB ext4 disk
#   make create-disk hints        # Show help
create-disk:
	@ARGS="$(filter-out $@,$(MAKECMDGOALS))"; \
	if [ -z "$$ARGS" ]; then \
		./scripts/create_disk.sh; \
	elif [ "$$ARGS" = "hints" ] || [ "$$ARGS" = "help" ]; then \
		./scripts/create_disk.sh --help; \
	else \
		./scripts/create_disk.sh $$ARGS; \
	fi
	@if [ "$$ARGS" != "hints" ] && [ "$$ARGS" != "help" ]; then \
		echo "Disk image is ready: disk.img"; \
	fi

# Delete disk image (useful to reset persistent disk for QEMU)
# Usage: make delete-disk [filesystem] [filename]
# Examples:
#   make delete-disk              # Delete disk.img (default)
#   make delete-disk minix        # Delete disk.img
#   make delete-disk fat32        # Delete fat32.img
#   make delete-disk fat32 fat32.img  # Delete fat32.img explicitly
delete-disk:
	@ARGS="$(filter-out $@,$(MAKECMDGOALS))"; \
	if [ -z "$$ARGS" ]; then \
		./scripts/delete_disk.sh; \
	else \
		./scripts/delete_disk.sh $$ARGS; \
	fi

# Load Init binary into virtual disk
# Usage: make load-init [filesystem] [disk_image] [init_binary]
# Defaults: filesystem=auto-detect, disk_image=disk.img, init_binary=setup/pid1/init
# Supported filesystems: minix, fat32, ext4
# Note: Requires root privileges (mounts filesystem)
# Examples:
#   sudo make load-init                    # Auto-detect, use disk.img
#   sudo make load-init fat32              # Use fat32.img
#   sudo make load-init fat32 fat32.img    # Explicit filesystem and disk
#   make load-init hints                   # Show help
load-init:
	@ARGS="$(filter-out $@,$(MAKECMDGOALS))"; \
	if [ -z "$$ARGS" ]; then \
		./scripts/load_init.sh; \
	elif [ "$$ARGS" = "hints" ] || [ "$$ARGS" = "help" ]; then \
		./scripts/load_init.sh --help; \
	else \
		./scripts/load_init.sh $$ARGS; \
	fi

# Remove Init binary from virtual disk
# Usage: make remove-init [filesystem] [disk_image]
# Defaults: filesystem=auto-detect, disk_image=disk.img
# Supported filesystems: minix, fat32, ext4
# Note: Requires root privileges (mounts filesystem)
# Examples:
#   sudo make remove-init                  # Auto-detect, use disk.img
#   sudo make remove-init fat32            # Use fat32.img
#   sudo make remove-init fat32 fat32.img  # Explicit filesystem and disk
#   make remove-init hints                 # Show help
remove-init:
	@ARGS="$(filter-out $@,$(MAKECMDGOALS))"; \
	if [ -z "$$ARGS" ]; then \
		./scripts/remove_init.sh; \
	elif [ "$$ARGS" = "hints" ] || [ "$$ARGS" = "help" ]; then \
		./scripts/remove_init.sh --help; \
	else \
		./scripts/remove_init.sh $$ARGS; \
	fi

# CLEAN

clean:
	@echo "Cleaning build artifacts..."
	@find . -name "*.o" -type f -delete
	@find . -name "*.d" -type f -delete
	@find . -name "*.bin" -type f -delete
	@find . -name "*.iso" -type f -delete
	@rm -rf iso/
	@rm -f qemu_debug.log
	@rm -f .example_drivers_enabled
	@echo "✓ Clean complete"

# HELP

help:
	@echo "╔════════════════════════════════════════════════════════════╗"
	@echo "║               IR0 KERNEL - BUILD SYSTEM                    ║"
	@echo "╚════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "Build:"
	@echo "  make ir0              Build kernel ISO"
	@echo "  make ir0 windows      Build kernel for Windows (cross-compile)"
	@echo "  make ir0 win          Alias for 'make ir0 windows'"
	@echo "  make clean            Clean all build artifacts"
	@echo "  make windows-clean    Clean Windows build artifacts"
	@echo ""
	@echo "Run:"
	@echo "  make run              Run with GUI + disk (recommended)"
	@echo "  make run-debug        Run with GUI + serial debug output"
	@echo "  make debug            Quick debug - serial output only"
	@echo "  make run-nodisk       Run without disk"
	@echo "  make run-console      Run in console mode"
	@echo ""
	@echo "Utilities:"
	@echo "  make menuconfig       Kernel configuration menu (experimental)"
	@echo "  make deptest          Check all dependencies (run this first!)"
	@echo "  make create-disk      Create virtual disk (MINIX by default)"
	@echo "  make create-disk hints  Show create-disk help"
	@echo "  make delete-disk      Delete virtual disk"
	@echo "  make load-init        Load Init binary into disk (requires sudo)"
	@echo "  make load-init hints  Show load-init help"
	@echo "  make remove-init      Remove Init binary from disk (requires sudo)"
	@echo "  make remove-init hints  Show remove-init help"
	@echo "  make help             Show this help"
	@echo ""
	@echo "Extern Drivers (Multi-Language):"
	@echo "  make en-ext-drv       Enable example drivers for next build"
	@echo "  make dis-ext-drv      Disable example drivers"
	@echo "  Note: Extern drivers are optional and disabled by default"
	@echo ""
	@echo "Unibuild - Isolated Compilation:"
	@echo "  C files (can compile multiple at once):"
	@echo "    make unibuild <file1.c> [file2.c] ..."
	@echo "    make unibuild-win <file1.c> [file2.c] ...     (Windows)"
	@echo ""
	@echo "  C++ files (can compile multiple at once):"
	@echo "    make unibuild-cpp <file1.cpp> [file2.cpp] ..."
	@echo "    make unibuild-cpp-win <file1.cpp> [file2.cpp] ...  (Windows)"
	@echo ""
	@echo "  Rust files (can compile multiple at once):"
	@echo "    make unibuild-rust <file1.rs> [file2.rs] ..."
	@echo "    make unibuild-rust-win <file1.rs> [file2.rs] ...   (Windows)"
	@echo ""
	@echo "  Clean:"
	@echo "    make unibuild-clean <file>"
	@echo ""
	@echo "  Examples:"
	@echo "    make unibuild fs/ramfs.c"
	@echo "    make unibuild fs/ramfs.c fs/vfs.c fs/path.c"
	@echo "    make unibuild-cpp cpp/examples/cpp_example.cpp"
	@echo "    make unibuild-rust rust/drivers/rust_example_driver.rs"
	@echo "    make unibuild-win drivers/IO/ps2.c"
	@echo "    make unibuild-cpp-win cpp/examples/cpp_example.cpp"
	@echo ""
	@echo "  Note: You can also use \"file1 file2\" syntax"
	@echo ""
	@echo "Quick start: make run"
	@echo ""

# DEPENDENCY TEST

deptest:
	@$(KERNEL_ROOT)/scripts/deptest.sh

# UNIBUILD - ISOLATED FILE COMPILATION

# Helper to get file arguments (supports multiple files from FILE= or positional)
# Usage in targets: $(call get-file-arg)
define get-file-arg
$(if $(FILE),$(FILE),$(filter-out $@,$(MAKECMDGOALS)))
endef

# Standard C compilation (supports multiple files)
unibuild:
	@FILE_ARG="$(call get-file-arg)"; \
	if [ -z "$$FILE_ARG" ]; then \
		echo "Error: No file specified"; \
		echo "Usage: make unibuild <file1.c> [file2.c] ..."; \
		echo "   Or: make unibuild \"<file1.c>  <file2.c>\""; \
		echo "Example: make unibuild fs/ramfs.c"; \
		echo "Example: make unibuild fs/ramfs.c fs/vfs.c"; \
		exit 1; \
	fi; \
	$(KERNEL_ROOT)/scripts/unibuild.sh $$FILE_ARG

# C++ compilation (supports multiple files)
unibuild-cpp:
	@FILE_ARG="$(call get-file-arg)"; \
	if [ -z "$$FILE_ARG" ]; then \
		echo "Error: No file specified"; \
		echo "Usage: make unibuild-cpp <file1.cpp> [file2.cpp] ..."; \
		echo "   Or: make unibuild-cpp \"<file1.cpp> <file2.cpp>\""; \
		echo "Example: make unibuild-cpp cpp/examples/cpp_example.cpp"; \
		exit 1; \
	fi; \
	$(KERNEL_ROOT)/scripts/unibuild.sh -cpp $$FILE_ARG

# Rust compilation (supports multiple files)
unibuild-rust:
	@FILE_ARG="$(call get-file-arg)"; \
	if [ -z "$$FILE_ARG" ]; then \
		echo "Error: No file specified"; \
		echo "Usage: make unibuild-rust <file1.rs> [file2.rs] ..."; \
		echo "   Or: make unibuild-rust \"<file1.rs> <file2.rs>\""; \
		echo "Example: make unibuild-rust rust/drivers/rust_example_driver.rs"; \
		exit 1; \
	fi; \
	$(KERNEL_ROOT)/scripts/unibuild.sh -rust $$FILE_ARG

# Windows cross-compilation (C, supports multiple files)
unibuild-win:
	@FILE_ARG="$(call get-file-arg)"; \
	if [ -z "$$FILE_ARG" ]; then \
		echo "Error: No file specified"; \
		echo "Usage: make unibuild-win <file1.c> [file2.c] ..."; \
		echo "   Or: make unibuild-win \"<file1.c> <file2.c>\""; \
		echo "Example: make unibuild-win drivers/IO/ps2.c"; \
		exit 1; \
	fi; \
	$(KERNEL_ROOT)/scripts/unibuild.sh -win $$FILE_ARG

# Windows cross-compilation (C++, supports multiple files)
unibuild-cpp-win:
	@FILE_ARG="$(call get-file-arg)"; \
	if [ -z "$$FILE_ARG" ]; then \
		echo "Error: No file specified"; \
		echo "Usage: make unibuild-cpp-win <file1.cpp> [file2.cpp] ..."; \
		echo "   Or: make unibuild-cpp-win \"<file1.cpp> <file2.cpp>\""; \
		echo "Example: make unibuild-cpp-win cpp/examples/cpp_example.cpp"; \
		exit 1; \
	fi; \
	$(KERNEL_ROOT)/scripts/unibuild.sh -win -cpp $$FILE_ARG

# Windows cross-compilation (Rust, supports multiple files)
unibuild-rust-win:
	@FILE_ARG="$(call get-file-arg)"; \
	if [ -z "$$FILE_ARG" ]; then \
		echo "Error: No file specified"; \
		echo "Usage: make unibuild-rust-win <file1.rs> [file2.rs] ..."; \
		echo "   Or: make unibuild-rust-win \"<file1.rs> <file2.rs>\""; \
		echo "Example: make unibuild-rust-win rust/drivers/rust_example_driver.rs"; \
		exit 1; \
	fi; \
	$(KERNEL_ROOT)/scripts/unibuild.sh -win -rust $$FILE_ARG

# Catch-all target to prevent make from complaining about unknown targets (for positional args)
%:
	@:

unibuild-clean:
	@if [ -z "$(FILE)" ]; then \
		echo "Error: FILE parameter required"; \
		echo "Usage: make unibuild-clean <source_file>"; \
		echo "Example: make unibuild-clean fs/ramfs.c"; \
		exit 1; \
	fi
	@$(KERNEL_ROOT)/scripts/unibuild-clean.sh "$(FILE)"

# TEST DRIVERS - MULTI-LANGUAGE EXAMPLES (OPTIONAL)

# Rust test driver
RUST_TEST_DRIVER = rust/drivers/rust_example_driver.rs
RUST_TEST_OBJ = rust/drivers/rust_example_driver.o

# C++ test driver
CPP_TEST_DRIVER = cpp/examples/cpp_example.cpp
CPP_TEST_OBJ = cpp/examples/cpp_example.o

# Enable example drivers for next build
en-ext-drv:
	@echo "ENABLE_EXAMPLE_DRIVERS=1" > .example_drivers_enabled
	@echo "✓ Example drivers enabled - will be compiled on next build"

# Disable example drivers
dis-ext-drv:
	@rm -f .example_drivers_enabled
	@echo "✓ Example drivers disabled - will not be compiled"

# Compile Rust test driver using unibuild
test-driver-rust: $(RUST_TEST_DRIVER)
	@echo "🦀 Compiling Rust test driver with unibuild..."
	@$(KERNEL_ROOT)/scripts/unibuild.sh -rust $(RUST_TEST_DRIVER)
	@if [ -f $(RUST_TEST_OBJ) ]; then \
		echo "✓ Rust driver compiled: $(RUST_TEST_OBJ)"; \
	else \
		echo "✗ Rust driver compilation failed"; \
		exit 1; \
	fi

# Compile C++ test driver using unibuild
test-driver-cpp: $(CPP_TEST_DRIVER)
	@echo "➕ Compiling C++ test driver with unibuild..."
	@$(KERNEL_ROOT)/scripts/unibuild.sh -cpp $(CPP_TEST_DRIVER)
	@if [ -f $(CPP_TEST_OBJ) ]; then \
		echo "✓ C++ driver compiled: $(CPP_TEST_OBJ)"; \
	else \
		echo "✗ C++ driver compilation failed"; \
		exit 1; \
	fi

# Compile all test drivers (standalone compilation, not included in kernel)
test-drivers: test-driver-rust test-driver-cpp
	@echo ""
	@echo "╔════════════════════════════════════════════════════════════╗"
	@echo "║          TEST DRIVERS COMPILATION COMPLETE                 ║"
	@echo "╚════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "✓ Rust driver:  $(RUST_TEST_OBJ)"
	@echo "✓ C++ driver:   $(CPP_TEST_OBJ)"
	@echo ""
	@echo "Note: These are standalone objects. To include in kernel build:"
	@echo "      make en-ext-drv && make ir0"
	@echo "      make dis-ext-drv && make ir0"
	@echo ""

# Clean test driver objects
# Clean test driver objects
test-drivers-clean:
	@echo "Cleaning test driver objects..."
	@if [ -n "$(RUST_ONLY)" ]; then \
		rm -f $(RUST_TEST_OBJ); \
		echo "✓ Rust driver objects cleaned"; \
	elif [ -n "$(CPP_ONLY)" ]; then \
		rm -f $(CPP_TEST_OBJ); \
		echo "✓ C++ driver objects cleaned"; \
	else \
		rm -f $(RUST_TEST_OBJ); \
		rm -f $(CPP_TEST_OBJ); \
		echo "✓ All test driver objects cleaned"; \
	fi


# PHONY TARGETS

.PHONY: all clean run run-nodisk run-console debug create-disk delete-disk load-init remove-init help \
        unibuild unibuild-cpp unibuild-rust unibuild-win unibuild-cpp-win unibuild-rust-win unibuild-clean \
        ir0 ir0-auto auto windows win windows-clean win-clean deptest \
        en-ext-drv dis-ext-drv \
        test-driver-rust test-driver-cpp test-drivers test-drivers-clean

# Include dependency files
-include $(ALL_OBJS:.o=.d)
