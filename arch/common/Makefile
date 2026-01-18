# ===============================================================================
# IR0 KERNEL - COMMON ARCHITECTURE MAKEFILE
# ===============================================================================
# This Makefile provides common build rules for all architectures

# ===============================================================================
# ARCHITECTURE DETECTION
# ===============================================================================

# Detect architecture from compiler
ifeq ($(shell $(CC) -dumpmachine | grep -q x86_64 && echo yes),yes)
    ARCH := x86-64
    ARCH_DIR := x86-64
    ARCH_CFLAGS := -m64 -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2
    ARCH_LDFLAGS := -m elf_x86_64
    ARCH_ASM_FORMAT := elf64
else ifeq ($(shell $(CC) -dumpmachine | grep -q i386 && echo yes),yes)
    ARCH := x86-32
    ARCH_DIR := x86-32
    ARCH_CFLAGS := -m32 -march=i386 -mno-mmx -mno-sse -mno-sse2
    ARCH_LDFLAGS := -m elf_i386
    ARCH_ASM_FORMAT := elf32
else ifeq ($(shell $(CC) -dumpmachine | grep -q aarch64 && echo yes),yes)
    ARCH := arm-64
    ARCH_DIR := arm-64
    ARCH_CFLAGS := -march=armv8-a -mcpu=cortex-a53
    ARCH_LDFLAGS := -m aarch64elf
    ARCH_ASM_FORMAT := elf64
else ifeq ($(shell $(CC) -dumpmachine | grep -q arm && echo yes),yes)
    ARCH := arm-32
    ARCH_DIR := arm-32
    ARCH_CFLAGS := -march=armv7-a -mcpu=cortex-a7 -mfpu=neon-vfpv4
    ARCH_LDFLAGS := -m armelf
    ARCH_ASM_FORMAT := elf32
else
    $(error "Arquitectura no soportada: $(shell $(CC) -dumpmachine)")
endif

# ===============================================================================
# COMMON VARIABLES
# ===============================================================================

# Common flags for all architectures
COMMON_CFLAGS := -nostdlib -nostdinc -fno-builtin -fno-stack-protector -fno-pic -nodefaultlibs -ffreestanding
COMMON_CFLAGS += -Wall -Wextra -O1 -MMD -MP

# Include paths
INCLUDE_PATHS := -I$(ROOT_DIR)/includes -I$(ROOT_DIR)/includes/ir0 -I$(ROOT_DIR)/arch/common
INCLUDE_PATHS += -I$(ROOT_DIR)/arch/$(ARCH_DIR)/include -I$(ROOT_DIR)/setup -I$(ROOT_DIR)/mm
INCLUDE_PATHS += -I$(ROOT_DIR)/mm/arch/$(ARCH_DIR) -I$(ROOT_DIR)/interrupt -I$(ROOT_DIR)/drivers
INCLUDE_PATHS += -I$(ROOT_DIR)/fs -I$(ROOT_DIR)/kernel -I$(ROOT_DIR)/examples

# Combine flags
CFLAGS := $(ARCH_CFLAGS) $(COMMON_CFLAGS) $(INCLUDE_PATHS)
LDFLAGS := $(ARCH_LDFLAGS)

# ===============================================================================
# COMMON RULES
# ===============================================================================

# Compile C source files
%.o: %.c
	@echo "Compilando $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Assemble ASM files
%.o: %.asm
	@echo "Ensamblando $<..."
	nasm -f $(ARCH_ASM_FORMAT) $< -o $@

# Compile architecture-specific C files
arch/$(ARCH_DIR)/%.o: arch/$(ARCH_DIR)/%.c
	@echo "Compilando $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Assemble architecture-specific ASM files
arch/$(ARCH_DIR)/%.o: arch/$(ARCH_DIR)/%.asm
	@echo "Ensamblando $<..."
	nasm -f $(ARCH_ASM_FORMAT) $< -o $@

# ===============================================================================
# ARCHITECTURE-SPECIFIC OBJECTS
# ===============================================================================

# Common architecture objects
ARCH_COMMON_OBJS := arch/common/arch_interface.o

# Architecture-specific objects
ifeq ($(ARCH),x86-64)
    ARCH_OBJS := arch/x86-64/sources/arch_x64.o \
                 arch/x86-64/sources/idt_arch_x64.o \
                 arch/x86-64/sources/fault.o \
                 arch/x86-64/sources/tss_x64.o \
                 arch/x86-64/asm/boot_x64.o
else ifeq ($(ARCH),x86-32)
    ARCH_OBJS := arch/x86-32/sources/arch_x86.o \
                 arch/x86-32/sources/idt_arch_x86.o \
                 arch/x86-32/asm/boot_x86.o
else ifeq ($(ARCH),arm-64)
    ARCH_OBJS := arch/arm-64/sources/arch_arm64.o \
                 arch/arm-64/sources/interrupt_arm64.o \
                 arch/arm-64/sources/timer_arm64.o \
                 arch/arm-64/asm/boot_arm64.o
else ifeq ($(ARCH),arm-32)
    ARCH_OBJS := arch/arm-32/sources/arch_arm32.o \
                 arch/arm-32/sources/interrupt_arm32.o \
                 arch/arm-32/sources/timer_arm32.o \
                 arch/arm-32/asm/boot_arm32.o
endif

# ===============================================================================
# CLEAN RULES
# ===============================================================================

clean-arch:
	@echo "Limpiando archivos de arquitectura..."
	rm -f $(ARCH_COMMON_OBJS) $(ARCH_OBJS)
	rm -f arch/*/*.d

# ===============================================================================
# HELP TARGET
# ===============================================================================

help-arch:
	@echo "=== IR0 Kernel - Arquitectura $(ARCH) ==="
	@echo "Arquitectura detectada: $(ARCH)"
	@echo "Directorio: arch/$(ARCH_DIR)"
	@echo "Flags de compilación: $(ARCH_CFLAGS)"
	@echo "Flags de enlazado: $(ARCH_LDFLAGS)"
	@echo "Formato ASM: $(ARCH_ASM_FORMAT)"
	@echo ""
	@echo "Objetos comunes: $(ARCH_COMMON_OBJS)"
	@echo "Objetos específicos: $(ARCH_OBJS)"
	@echo ""
	@echo "Comandos disponibles:"
	@echo "  clean-arch    - Limpiar archivos de arquitectura"
	@echo "  help-arch     - Mostrar esta ayuda"
