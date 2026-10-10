# SPDX-License-Identifier: GPL-3.0-only
# Architecture-specific object inventories and freestanding ARM64 exceptions.

ARCH_OBJS_COMMON = \
	arch/common/boot_log.o

ARCH_OBJS_X86_64 = \
	arch/x86-64/sources/arch_debug.o \
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
	arch/common/boot_log.o \
	arch/arm64/sources/arch_debug.o \
	arch/arm64/sources/arch_arm64.o \
	arch/arm64/sources/arch_early.o \
	arch/arm64/sources/interrupts.o \
	arch/arm64/sources/timer.o \
	arch/arm64/sources/mm_ops.o \
	arch/arm64/sources/pl011.o \
	arch/arm64/sources/serial_io_arm64.o \
	arch/arm64/sources/gic_v2.o \
	arch/arm64/sources/bcm2836_irq.o \
	arch/arm64/sources/syscall_decode.o \
	arch/arm64/sources/elf_policy.o

# The freestanding path intentionally provides the Linux syscall ABI in small
# stages while the common process and VFS implementations are brought up.
ARM64_EARLY_SYSCALL_OBJS = \
	arch/arm64/sources/syscall_early.o \
	arch/arm64/sources/syscall_mm_early.o \
	arch/arm64/sources/syscall_vfs_early.o \
	arch/arm64/sources/syscall_time_early.o \
	arch/arm64/sources/syscall_signal_early.o \
	arch/arm64/sources/syscall_process_early.o \
	arch/arm64/sources/syscall_io_early.o \
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
	arch/arm64/sources/arch_page_fault.o

# Minimal runtime services needed before the complete kernel service graph is
# available. These are real freestanding providers, not link-only shims.
ARM64_FREESTANDING_RUNTIME_OBJS = \
	arch/arm64/sources/freestanding_stubs.o \
	arch/arm64/asm/context/switch_early.o \
	arch/arm64/sources/switch_early.o \
	arch/arm64/sources/process_early.o

# EL0 handoff and the embedded userspace image used by the QEMU ARM64 smoke.
ARM64_EARLY_USERSPACE_OBJS = \
	arch/arm64/sources/elf_load_early.o \
	arch/arm64/sources/hello_embed.o \
	arch/arm64/sources/busybox_load_early.o \
	arch/arm64/sources/init_handoff_early.o \
	arch/arm64/sources/rootfs_early.o \
	arch/arm64/sources/busybox_embed.o

# The round-robin scheduler is compiled with the freestanding ARM flags until
# the full scheduler/provider graph is available on this architecture.
ARM64_EARLY_SCHEDULER_OBJS = \
	arch/arm64/sources/rr_early.o \
	arch/arm64/sources/rr_early_stubs.o

# QEMU virtio block support is part of the early usable-machine path.
ARM64_EARLY_STORAGE_OBJS = \
	arch/arm64/sources/virtio_blk_early.o \
	drivers/virtio/virtio_mmio.o

ARM64_FIRST_SWITCH_OBJS = \
	arch/arm64/sources/first_switch.o \
	build/arm64-boot/rr_sched.o \
	build/arm64-boot/switch_arm64.o

# Deliberately excluded from the real ARM64 boot image. They are only used by
# kernel-arm64-min.bin and kernel-arm64-all.bin link-probe targets in testing.mk.
ARM64_LINK_PROBE_ONLY_OBJS = \
	arch/arm64/sources/min_link_stubs.o

ARCH_OBJS_ARM64 = \
	$(ARM64_BACKEND_OBJS) \
	$(ARM64_EARLY_SYSCALL_OBJS) \
	$(ARM64_PROCESS_BACKEND_OBJS) \
	$(ARM64_FREESTANDING_RUNTIME_OBJS) \
	$(ARM64_EARLY_USERSPACE_OBJS) \
	$(ARM64_EARLY_SCHEDULER_OBJS) \
	$(ARM64_EARLY_STORAGE_OBJS) \
	$(ARM64_FIRST_SWITCH_OBJS)

.PHONY: arm64-staging-audit

# Prevent a link-probe shim from silently becoming part of a bootable image.
arm64-staging-audit:
	@if [ -n "$(filter $(ARM64_LINK_PROBE_ONLY_OBJS),$(ARCH_OBJS_ARM64))" ]; then \
		echo "arm64-staging-audit: link-probe objects leaked into ARCH_OBJS_ARM64" >&2; \
		exit 1; \
	fi
	@echo "[arm64-staging-audit] OK (link probes excluded; staged sets classified)"

build/arm64-boot/rr_sched.o: sched/rr_sched.c
	@mkdir -p build/arm64-boot
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
		-I$(KERNEL_ROOT)/sched -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -I$(KERNEL_ROOT)/arch/common \
		-I$(KERNEL_ROOT) \
		-c sched/rr_sched.c -o build/arm64-boot/rr_sched.o

build/arm64-boot/switch_arm64.o: sched/switch/switch_arm64.c
	@mkdir -p build/arm64-boot
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/sched \
		-I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 \
		-I$(KERNEL_ROOT)/arch/common \
		-c sched/switch/switch_arm64.c -o build/arm64-boot/switch_arm64.o

ifeq ($(ARCH),arm64)
ARCH_OBJS := $(ARCH_OBJS_ARM64)
else
ARCH_OBJS := $(ARCH_OBJS_COMMON) $(ARCH_OBJS_X86_64)
endif
