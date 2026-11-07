# SPDX-License-Identifier: GPL-3.0-only
# Architecture-specific object inventories (ARM64 portable product path).

ARCH_COMMON_BOOT_LOG_OBJ = build/$(ARCH)/arch/common/boot_log.o

ARCH_OBJS_COMMON = \
	$(ARCH_COMMON_BOOT_LOG_OBJ)

ARCH_OBJS_X86_64 = \
	arch/x86-64/sources/boot_firmware_sysfs_arch.o \
	arch/x86-64/sources/x86_boot_port.o \
	arch/x86-64/sources/x86_oops_port.o \
	arch/x86-64/sources/arch_debug.o \
	arch/x86-64/sources/debug_trap.o \
	arch/x86-64/sources/arch_interface.o \
	arch/x86-64/sources/arch_x64.o \
	arch/x86-64/sources/platform.o \
	arch/x86-64/sources/gdt.o \
	arch/x86-64/sources/tss_x64.o \
	arch/x86-64/sources/arch_early.o \
	arch/x86-64/sources/user_mode.o \
	arch/x86-64/sources/arch_fork.o \
	arch/x86-64/sources/fork_asm_hooks.o \
	arch/x86-64/sources/arch_task_ops.o \
	arch/x86-64/sources/task_stack.o \
	arch/x86-64/sources/syscall_decode.o \
	arch/x86-64/sources/elf_policy.o \
	arch/x86-64/sources/arch_syscall_frame.o \
	arch/x86-64/sources/arch_signal.o \
	arch/x86-64/sources/arch_switch.o \
	arch/x86-64/sources/switch_diag.o \
	arch/x86-64/sources/arch_mm.o \
	arch/x86-64/sources/arch_irq_init.o \
	arch/x86-64/sources/arch_page_fault.o \
	arch/x86-64/sources/idt_arch_x64.o \
	arch/x86-64/sources/fault.o \
	arch/x86-64/sources/debug_dump.o \
	arch/x86-64/sources/arch_pf_debug.o \
	arch/x86-64/sources/mm_ops.o \
	arch/x86-64/vdso/blob.o \
	arch/x86-64/vdso/vdso_blob_embed.o \
	arch/x86-64/asm/boot/boot_x64.o \
	arch/x86-64/asm/entry/syscall_int80.o \
	arch/x86-64/asm/entry/syscall_64.o \
	arch/x86-64/asm/context/switch_x64.o

# ARM64 backend providers used by both the normal and freestanding paths.
ARM64_BACKEND_OBJS = \
	$(ARCH_COMMON_BOOT_LOG_OBJ) \
	arch/arm64/sources/boot_firmware_sysfs_arch.o \
	arch/arm64/sources/arm64_boot_port.o \
	arch/arm64/sources/arch_debug.o \
	arch/arm64/sources/arch_arm64.o \
	arch/arm64/sources/board.o \
	arch/arm64/sources/boot_info.o \
	arch/arm64/sources/irq_backend.o \
	arch/arm64/sources/elf_image_early.o \
	arch/arm64/sources/arm64_boot_common.o \
	arch/arm64/sources/arch_early.o \
	arch/arm64/sources/interrupts.o \
	arch/arm64/sources/timer.o \
	arch/arm64/sources/rtc.o \
	arch/arm64/sources/vdso.o \
	arch/arm64/sources/mm_ops.o \
	arch/arm64/sources/pl011.o \
	arch/arm64/sources/serial_io_arm64.o \
	arch/arm64/sources/gic_v2.o \
	arch/arm64/sources/bcm2836_irq.o \
	arch/arm64/sources/syscall_decode.o \
	arch/arm64/sources/elf_policy.o

# Portable kernel: ir0/cpu.h + irq.h facades (replaces freestanding_stubs subset).
ARM64_INTERFACE_OBJS_PRODUCT = \
	arch/arm64/sources/arch_interface.o

# The freestanding path intentionally provides the Linux syscall ABI in small
# stages while the common process and VFS implementations are brought up.
ARM64_EARLY_SYSCALL_OBJS = \
	arch/arm64/sources/usercopy_early.o \
	arch/arm64/sources/syscall_early.o \
	arch/arm64/sources/boot_stub.o \
	arch/arm64/sources/mmu_early.o \
	arch/arm64/sources/exc_early.o \
	arch/arm64/sources/slice_hello.o \
	arch/arm64/sources/portable_string.o \
	arch/arm64/asm/entry/vectors.o \
	arch/arm64/sources/syscall_stub.o

# Architecture implementations of the generic process, MM and context facade.
ARM64_PROCESS_BACKEND_OBJS = \
	arch/arm64/sources/platform.o \
	arch/arm64/sources/arch_fork.o \
	arch/arm64/sources/fork_asm_hooks.o \
	arch/arm64/sources/arch_task_ops.o \
	arch/arm64/sources/arch_tls.o \
	arch/arm64/sources/task_stack.o \
	arch/arm64/sources/arch_syscall_frame.o \
	arch/arm64/sources/arch_signal.o \
	arch/arm64/sources/arch_switch.o \
	arch/arm64/sources/arch_mm.o \
	arch/arm64/sources/arch_irq_init.o \
	arch/arm64/sources/arch_page_fault.o \
	arch/arm64/sources/arch_pf_debug.o \
	arch/arm64/sources/user_mode.o \
	arch/arm64/sources/acpi_pm_stub.o

# Minimal runtime services needed before the complete kernel service graph is
# available. These are real freestanding providers, not link-only shims.
ARM64_FREESTANDING_RUNTIME_OBJS = \
	arch/arm64/sources/freestanding_stubs.o \
	arch/arm64/asm/context/switch_early.o \
	arch/arm64/sources/switch_early.o \
	arch/arm64/sources/process_early.o

ARM64_FREESTANDING_RUNTIME_OBJS_PRODUCT = \
	arch/arm64/asm/context/switch_early.o \
	arch/arm64/sources/switch_early.o \
	arch/arm64/sources/process_early.o

# EL0 handoff and the embedded userspace image used by the QEMU ARM64 smoke.
ARM64_EARLY_USERSPACE_OBJS = \
	arch/arm64/sources/elf_load_early.o \
	arch/arm64/sources/hello_embed.o \
	arch/arm64/sources/embedded_elf_early.o \
	arch/arm64/sources/init_handoff_early.o \
	arch/arm64/sources/rootfs_early.o \
	arch/arm64/sources/embedded_elf_embed.o

# The round-robin scheduler is compiled with the freestanding ARM flags until
# the full scheduler/provider graph is available on this architecture.
ARM64_EARLY_SCHEDULER_OBJS = \
	arch/arm64/sources/rr_early.o

# QEMU virtio block support is part of the early usable-machine path.
ARM64_EARLY_STORAGE_OBJS = \
	arch/arm64/sources/virtio_blk_early.o \
	arch/arm64/sources/virtio_net_early.o \
	drivers/virtio/virtio_mmio.o

ARM64_FIRST_SWITCH_OBJS = \
	arch/arm64/sources/first_switch.o \
	sched/switch/switch_arm64.o

# ktm/mocks/arm64/min_link_stubs.c is not part of ARCH_OBJS_ARM64.

$(ARCH_COMMON_BOOT_LOG_OBJ): arch/common/boot_log.c
	@mkdir -p $(@D)
	@echo "  CC      $< ($(ARCH) namespace)"
	@$(CC) $(CFLAGS) -c $< -o $@

ARCH_OBJS_ARM64 = \
	arch/arm64/asm/boot/entry.o \
	$(ARM64_BACKEND_OBJS) \
	$(ARM64_INTERFACE_OBJS_PRODUCT) \
	$(filter-out arch/arm64/sources/syscall_stub.o,$(ARM64_EARLY_SYSCALL_OBJS)) \
	$(ARM64_PROCESS_BACKEND_OBJS) \
	$(ARM64_FREESTANDING_RUNTIME_OBJS_PRODUCT) \
	$(ARM64_EARLY_USERSPACE_OBJS) \
	$(ARM64_EARLY_SCHEDULER_OBJS) \
	$(ARM64_EARLY_STORAGE_OBJS) \
	$(ARM64_FIRST_SWITCH_OBJS)

ifeq ($(ARCH),arm64)
ARCH_OBJS := $(ARCH_OBJS_ARM64)
else
ARCH_OBJS := $(ARCH_OBJS_COMMON) $(ARCH_OBJS_X86_64)
endif
