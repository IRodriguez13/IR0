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

# Hardware completo soportado por IR0
# Incluye: RTL8139, SB16, Adlib, ATA/IDE, Serial, PS/2 (default), VGA (default)
# Nota: e1000 desactivado temporalmente por problemas de inicialización
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

# KERNEL OBJECTS (CONSOLIDATED)

KERNEL_OBJS = \
	kernel/main.o \
    kernel/init.o \
    kernel/process.o \
    kernel/process_util.o \
	kernel/rr_sched.o \
    kernel/task.o \
    kernel/syscalls.o \
    kernel/dbgshell.o \
    kernel/elf_loader.o \
    kernel/user.o \
    kernel/driver_registry.o

MEMORY_OBJS = \
	includes/ir0/memory/allocator.o \
	includes/ir0/memory/paging.o \
	includes/ir0/memory/pmm.o \
	includes/ir0/memory/kmem.o

LIB_OBJS = \
    includes/ir0/vga.o \
    includes/ir0/logging.o \
    includes/ir0/validation.o \
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
	drivers/video/typewriter.o

FS_OBJS = \
    fs/vfs.o \
    fs/minix_fs.o \
    fs/vfs_simple.o \
    fs/path.o \
    fs/chmod.o \
    fs/ramfs.o

DISK_OBJS = \
    drivers/disk/partition.o

NET_OBJS = \
    net/net.o \
    net/arp.o

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
	setup/kconfig.o

CPP_OBJS = \
	cpp/runtime/compat.o

# All objects
ALL_OBJS = $(KERNEL_OBJS) $(MEMORY_OBJS) $(LIB_OBJS) $(INTERRUPT_OBJS) \
           $(DRIVER_OBJS) $(FS_OBJS) $(ARCH_OBJS) $(SETUP_OBJS) $(DISK_OBJS) $(CPP_OBJS) $(NET_OBJS)

# BUILD RULES

# Compile C files
%.o: %.c
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile C++ files
%.o: %.cpp
	@echo "  CXX     $<"
	@g++ -m64 -ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics \
		-mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
		-nostdlib -lgcc -g -Wall -Wextra -fno-stack-protector -fno-builtin \
		-I./cpp/include $(CFLAGS) -c $< -o $@

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
	@rm -rf iso
	@mkdir -p iso/boot/grub
	@cp arch/x86-64/grub.cfg iso/boot/grub/
	@cp kernel-x64.bin iso/boot/
	@echo "Running grub-mkrescue (will print errors if it fails)..."
	@grub-mkrescue -o $@ iso
	@echo "✓ ISO created: $@"

# Create a raw disk image used by QEMU. If you want a persistent filesystem
# you can format and populate this image separately. This target will
# create a 64 MiB raw disk image if it doesn't already exist.
disk.img:
	@if [ -f $@ ]; then \
		echo "  DISK    $@ already exists"; \
	else \
		echo "  DISK    creating $@ using scripts/create_disk.sh"; \
		./scripts/create_disk.sh; \
	fi
	@echo "✓ Disk image ready: $@"

# Default target
ir0: kernel-x64.iso

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
	@echo "🐛 Running IR0 Kernel with debug output and all supported hardware..."
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

# Run with minimal hardware (only essentials for testing)
run-minimal: kernel-x64.iso disk.img
	@echo "Running IR0 Kernel (minimal hardware)..."
	@echo "   Hardware: RTL8139 only, ATA/IDE, Serial"
	qemu-system-x86_64 -cdrom kernel-x64.iso \
		-netdev user,id=net0 -device rtl8139,netdev=net0 \
		$(QEMU_STORAGE_IDE) $(QEMU_SERIAL_COM1) \
		-m 512M -no-reboot -no-shutdown \
		$(QEMU_DISPLAY) \
		$(QEMU_DEBUG_GUEST) $(QEMU_LOG_FILE)

# Create disk image (wrapper for scripts/create_disk.sh)
create-disk: disk.img
	@echo "Disk image is ready: disk.img"

# Delete disk image (useful to reset persistent disk for QEMU)
delete-disk:
	@if [ -f disk.img ]; then \
		rm -f disk.img; \
		echo "✓ Disk image removed: disk.img"; \
	else \
		echo "  Disk image not found: disk.img"; \
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
	@echo "✓ Clean complete"

# HELP

help:
	@echo "╔════════════════════════════════════════════════════════════╗"
	@echo "║       IR0 KERNEL - BUILD SYSTEM (x86-64 ONLY)             ║"
	@echo "╚════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "📦 Build:"
	@echo "  make ir0              Build kernel ISO + userspace programs"
	@echo "  make ir0 windows      Build kernel for Windows (cross-compile)"
	@echo "  make ir0 win          Alias for 'make ir0 windows'"
	@echo "  make clean            Clean all build artifacts"
	@echo "  make windows-clean    Clean Windows build artifacts"
	@echo "  make userspace-programs  Build only userspace programs"
	@echo "  make userspace-clean     Clean only userspace programs"
	@echo ""
	@echo "🚀 Run:"
	@echo "  make run-kernel       Run with GUI + disk (recommended)"
	@echo "  make run-debug        Run with GUI + serial debug output"
	@echo "  make debug            Quick debug - serial output only"
	@echo "  make run-nodisk       Run without disk"
	@echo "  make run-console      Run in console mode"
	@echo ""
	@echo "🔧 Utilities:"
	@echo "  make deptest          Check all dependencies (run this first!)"
	@echo "  make create-disk      Create virtual disk for MINIX FS"
	@echo "  make delete-disk      Delete virtual disk for MINIX FS"
	@echo "  make help             Show this help"
	@echo ""
	@echo "🔨 Unibuild - Isolated Compilation:"
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
	@echo "    make unibuild-clean FILE=<file>"
	@echo ""
	@echo "  Examples:"
	@echo "    make unibuild fs/ramfs.c"
	@echo "    make unibuild fs/ramfs.c fs/vfs.c fs/path.c"
	@echo "    make unibuild-cpp cpp/examples/cpp_example.cpp"
	@echo "    make unibuild-rust rust/drivers/rust_example_driver.rs"
	@echo "    make unibuild-win drivers/IO/ps2.c"
	@echo "    make unibuild-cpp-win cpp/examples/cpp_example.cpp"
	@echo ""
	@echo "  Note: You can also use FILE=\"file1 file2\" syntax"
	@echo ""
	@echo "🦀 Test Drivers (Multi-Language):"
	@echo "  make test-drivers     Compile all test drivers (Rust + C++)"
	@echo "  make test-driver-rust Compile Rust test driver"
	@echo "  make test-driver-cpp  Compile C++ test driver"
	@echo "  make test-drivers-clean  Clean test driver objects"
	@echo ""
	@echo "💡 Quick start: make run"
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
		echo "   Or: make unibuild FILE=\"<file1.c> <file2.c>\""; \
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
		echo "   Or: make unibuild-cpp FILE=\"<file1.cpp> <file2.cpp>\""; \
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
		echo "   Or: make unibuild-rust FILE=\"<file1.rs> <file2.rs>\""; \
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
		echo "   Or: make unibuild-win FILE=\"<file1.c> <file2.c>\""; \
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
		echo "   Or: make unibuild-cpp-win FILE=\"<file1.cpp> <file2.cpp>\""; \
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
		echo "   Or: make unibuild-rust-win FILE=\"<file1.rs> <file2.rs>\""; \
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
		echo "Usage: make unibuild-clean FILE=<source_file>"; \
		echo "Example: make unibuild-clean FILE=fs/ramfs.c"; \
		exit 1; \
	fi
	@$(KERNEL_ROOT)/scripts/unibuild-clean.sh "$(FILE)"

# ============================================================================
# TEST DRIVERS - MULTI-LANGUAGE EXAMPLES
# ============================================================================

# Rust test driver
RUST_TEST_DRIVER = rust/drivers/rust_example_driver.rs
RUST_TEST_OBJ = rust/drivers/rust_example_driver.o

# C++ test driver
CPP_TEST_DRIVER = cpp/examples/cpp_example.cpp
CPP_TEST_OBJ = cpp/examples/cpp_example.o

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

# Compile all test drivers
test-drivers: test-driver-rust test-driver-cpp
	@echo ""
	@echo "╔════════════════════════════════════════════════════════════╗"
	@echo "║          TEST DRIVERS COMPILATION COMPLETE                 ║"
	@echo "╚════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "✓ Rust driver:  $(RUST_TEST_OBJ)"
	@echo "✓ C++ driver:   $(CPP_TEST_OBJ)"
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

# -------------------------------------------------------------------
# Load drivers on demand (convenient wrapper)
# Usage examples:
#   make load-driver rust          # compile and link all Rust drivers
#   make load-driver cpp           # compile and link all C++ drivers
#   make load-driver rust cpp      # compile and link both
# The arguments are parsed from MAKECMDGOALS

load-driver: $(if $(filter rust,$(MAKECMDGOALS)),load-driver-rust) $(if $(filter cpp,$(MAKECMDGOALS)),load-driver-cpp)
	@echo "Selected drivers have been compiled and linked."

load-driver-rust:
	$(MAKE) test-driver-rust

load-driver-cpp:
	$(MAKE) test-driver-cpp

# Unload drivers (clean objects)
# Usage: make unload-driver rust cpp
unload-driver: $(if $(filter rust,$(MAKECMDGOALS)),unload-driver-rust) $(if $(filter cpp,$(MAKECMDGOALS)),unload-driver-cpp)
	@echo "Selected drivers have been cleaned."

unload-driver-rust:
	$(MAKE) test-drivers-clean RUST_ONLY=1

unload-driver-cpp:
	$(MAKE) test-drivers-clean CPP_ONLY=1

# Prevent make from treating the arguments as separate targets
rust:
	@:

cpp:
	@:

# PHONY TARGETS

.PHONY: all clean run run-nodisk run-console debug create-disk help \
        unibuild unibuild-cpp unibuild-rust unibuild-win unibuild-cpp-win unibuild-rust-win unibuild-clean \
        ir0 windows win windows-clean win-clean deptest \
        test-driver-rust test-driver-cpp test-drivers test-drivers-clean

# Include dependency files
-include $(ALL_OBJS:.o=.d)
