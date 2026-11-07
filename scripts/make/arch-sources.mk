# SPDX-License-Identifier: GPL-3.0-only
# Architecture-specific object inventories (ARM64 portable product path).

ARCH_COMMON_BOOT_LOG_OBJ = build/$(ARCH)/arch/common/boot_log.o

ARCH_OBJS_COMMON = \
	$(ARCH_COMMON_BOOT_LOG_OBJ)

ARCH_OBJS_X86_64 = \
	arch/x86-64/sources/firmware_sysfs.o \
	arch/x86-64/sources/boot_port.o \
	arch/x86-64/sources/oops_port.o \
	arch/x86-64/sources/debug.o \
	arch/x86-64/sources/debug_trap.o \
	arch/x86-64/sources/interface.o \
	arch/x86-64/sources/platform.o \
	arch/x86-64/sources/gdt.o \
	arch/x86-64/sources/tss.o \
	arch/x86-64/sources/early.o \
	arch/x86-64/sources/user_mode.o \
	arch/x86-64/sources/fork.o \
	arch/x86-64/sources/fork_asm_hooks.o \
	arch/x86-64/sources/task.o \
	arch/x86-64/sources/task_stack.o \
	arch/x86-64/sources/syscall_decode.o \
	arch/x86-64/sources/elf_policy.o \
	arch/x86-64/sources/syscall_frame.o \
	arch/x86-64/sources/signal.o \
	arch/x86-64/sources/context.o \
	arch/x86-64/sources/switch_diag.o \
	arch/x86-64/sources/mm.o \
	arch/x86-64/sources/irq.o \
	arch/x86-64/sources/page_fault.o \
	arch/x86-64/sources/fault.o \
	arch/x86-64/sources/debug_dump.o \
	arch/x86-64/sources/pf_debug.o \
	arch/x86-64/sources/mm_ops.o \
	arch/x86-64/sources/dma.o \
	arch/x86-64/vdso/blob.o \
	arch/x86-64/vdso/vdso_blob_embed.o \
	arch/x86-64/asm/boot/boot_x64.o \
	arch/x86-64/asm/entry/syscall_int80.o \
	arch/x86-64/asm/entry/syscall_64.o \
	arch/x86-64/asm/context/switch_x64.o

# ARM64 backend providers used by both the normal and freestanding paths.
ARM64_BACKEND_OBJS = \
	$(ARCH_COMMON_BOOT_LOG_OBJ) \
	arch/arm64/sources/firmware_sysfs.o \
	arch/arm64/sources/boot_port.o \
	arch/arm64/sources/debug.o \
	arch/arm64/sources/cpu.o \
	arch/arm64/sources/board.o \
	arch/arm64/sources/boot_info.o \
	arch/arm64/sources/fdt_reader.o \
	arch/arm64/sources/boot_info_platform.o \
	arch/arm64/sources/boot_info_devices.o \
	arch/arm64/sources/irq_backend.o \
	arch/arm64/sources/boot_common.o \
	arch/arm64/sources/early.o \
	arch/arm64/sources/interrupts.o \
	arch/arm64/sources/timer.o \
	arch/arm64/sources/rtc.o \
	arch/arm64/sources/vdso.o \
	arch/arm64/sources/mm_ops.o \
	arch/arm64/sources/dma.o \
	arch/arm64/sources/pl011.o \
	arch/arm64/sources/serial.o \
	arch/arm64/sources/gic_v2.o \
	arch/arm64/sources/bcm2836_irq.o \
	arch/arm64/sources/syscall_decode.o \
	arch/arm64/sources/elf_policy.o

# Portable kernel: ir0/cpu.h + irq.h facades (replaces freestanding_stubs subset).
ARM64_INTERFACE_OBJS_PRODUCT = \
	arch/arm64/sources/interface.o

# ISA entry retained in the product image (name “early” is historical).
ARM64_PRODUCT_ENTRY_OBJS = \
	arch/arm64/sources/mmu.o \
	arch/arm64/sources/exc_early.o \
	arch/arm64/sources/portable_string.o \
	arch/arm64/asm/entry/vectors.o

# Architecture implementations of the generic process, MM and context facade.
ARM64_PROCESS_BACKEND_OBJS = \
	arch/arm64/sources/platform.o \
	arch/arm64/sources/fork.o \
	arch/arm64/sources/fork_asm_hooks.o \
	arch/arm64/sources/task.o \
	arch/arm64/sources/tls.o \
	arch/arm64/sources/task_stack.o \
	arch/arm64/sources/syscall_frame.o \
	arch/arm64/sources/signal.o \
	arch/arm64/sources/context.o \
	arch/arm64/sources/mm.o \
	arch/arm64/sources/irq.o \
	arch/arm64/sources/page_fault.o \
	arch/arm64/sources/pf_debug.o \
	arch/arm64/sources/user_mode.o \
	arch/arm64/sources/acpi_pm_stub.o

# Product needs the cooperative switch asm + mm helper (historical “early” names).
ARM64_PRODUCT_SWITCH_OBJS = \
	arch/arm64/asm/context/switch_early.o \
	arch/arm64/sources/switch_early.o

# Product keeps virtio-blk (CPIO/vda).
ARM64_PRODUCT_STORAGE_OBJS = \
	arch/arm64/sources/virtio_blk_early.o \
	drivers/virtio/virtio_mmio.o

ARM64_FIRST_SWITCH_OBJS = \
	arch/arm64/sources/first_switch.o \
	arch/arm64/sources/switch.o

# ktm/mocks/arm64/min_link_stubs.c is not part of ARCH_OBJS_ARM64.

$(ARCH_COMMON_BOOT_LOG_OBJ): arch/common/boot_log.c
	@mkdir -p $(@D)
	@echo "  CC      $< ($(ARCH) namespace)"
	@$(CC) $(CFLAGS) -c $< -o $@

# Product portable ARM64 kernel: common $(ALL_OBJS) path, no userspace bring-up.
ARCH_OBJS_ARM64 = \
	arch/arm64/asm/boot/entry.o \
	$(ARM64_BACKEND_OBJS) \
	$(ARM64_INTERFACE_OBJS_PRODUCT) \
	$(ARM64_PRODUCT_ENTRY_OBJS) \
	$(ARM64_PROCESS_BACKEND_OBJS) \
	$(ARM64_PRODUCT_SWITCH_OBJS) \
	$(ARM64_PRODUCT_STORAGE_OBJS) \
	$(ARM64_FIRST_SWITCH_OBJS)

ifeq ($(ARCH),arm64)
ARCH_OBJS := $(ARCH_OBJS_ARM64)
else
ARCH_OBJS := $(ARCH_OBJS_COMMON) $(ARCH_OBJS_X86_64)
endif
