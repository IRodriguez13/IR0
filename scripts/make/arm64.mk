# SPDX-License-Identifier: GPL-3.0-only
#
# ARM64 product recipes — lean fragment included by the root Makefile.
#
# Products:
#   kernel-arm64.bin              validated qemu-virt aggregate (default)
#   kernel-arm64-<board>-min.bin  early board images (rpi3/rpi4/rpi5)
#   kernel-arm64-Image            raw Linux arm64 Image header
#
# The early aggregate is self-contained: it embeds build/hello_aarch64 as its
# EL0 guest and rootfs seed. No ISD/BusyBox artifact is required to build it.

# Board selection. The root Makefile maps BOARD=... to ARM64_BOARD before this
# fragment is parsed; keep the qemu-virt default for bare `make ARCH=arm64`.
ARM64_BOARD ?= qemu-virt

ifeq ($(ARM64_BOARD),rpi3)
ARM64_BOARD_CFLAGS := -DIR0_ARM64_BOARD_RPI3=1
else ifeq ($(ARM64_BOARD),rpi4)
ARM64_BOARD_CFLAGS := -DIR0_ARM64_BOARD_RPI4=1
else ifeq ($(ARM64_BOARD),rpi5)
ARM64_BOARD_CFLAGS := -DIR0_ARM64_BOARD_RPI5=1
else
ARM64_BOARD_CFLAGS := -DIR0_ARM64_BOARD_QEMU_VIRT=1
ARM64_BOARD := qemu-virt
endif

# Repo-local musl cross toolchain (scripts/setup_musl_aarch64.sh). Fall back to
# a freestanding build when the musl CRT is unavailable, so a clean checkout
# still produces a bootable hello blob without a network fetch.
MUSL_CC_AARCH64 ?= $(shell \
	command -v aarch64-linux-musl-gcc 2>/dev/null || \
	(test -x $(KERNEL_ROOT)/toolchain/aarch64-linux-musl/bin/aarch64-linux-musl-gcc && \
		echo $(KERNEL_ROOT)/toolchain/aarch64-linux-musl/bin/aarch64-linux-musl-gcc) || \
	(test -x $(KERNEL_ROOT)/toolchain/aarch64-linux-musl-cross/bin/aarch64-linux-musl-gcc && \
		echo $(KERNEL_ROOT)/toolchain/aarch64-linux-musl-cross/bin/aarch64-linux-musl-gcc) || \
	true)
MUSL_AARCH64_HELLO := $(KERNEL_ROOT)/build/hello_aarch64
MUSL_AARCH64_HELLO_RPI3 := $(KERNEL_ROOT)/build/hello_aarch64_rpi3

.PHONY: setup-musl-aarch64 musl-aarch64-hello

setup-musl-aarch64:
	@bash $(KERNEL_ROOT)/scripts/setup_musl_aarch64.sh

$(KERNEL_ROOT)/build:
	@mkdir -p $@

$(MUSL_AARCH64_HELLO): setup/pid1/hello_aarch64.c setup/pid1/hello_aarch64_freestanding.c | $(KERNEL_ROOT)/build
	@set -e; \
	if [ -n "$(MUSL_CC_AARCH64)" ] && [ -x "$(MUSL_CC_AARCH64)" ] && \
	   [ "$$($(MUSL_CC_AARCH64) -print-file-name=crt1.o)" != "crt1.o" ]; then \
		echo "  CC      hello_aarch64 with $(MUSL_CC_AARCH64)"; \
		$(MUSL_CC_AARCH64) -static -fno-pie -no-pie -Os \
			-Wl,-Ttext-segment=0x43000000 \
			-o $@ $(KERNEL_ROOT)/setup/pid1/hello_aarch64.c; \
	else \
		echo "  CC      hello_aarch64 freestanding (musl CRT missing)"; \
		aarch64-linux-gnu-gcc -nostdlib -static -fno-pie -no-pie -Os \
			-Wl,-Ttext-segment=0x43000000 -Wl,-e,_start \
			-o $@ $(KERNEL_ROOT)/setup/pid1/hello_aarch64_freestanding.c; \
	fi
	@file $@ | grep -qi 'aarch64'
	@file $@ | grep -qi 'static'
	@echo "✓ musl-aarch64-hello → $@ (ET_EXEC @ 0x43000000)"

musl-aarch64-hello: $(MUSL_AARCH64_HELLO)
	@:

# Keep the image blob separate from its C loader. The generic %.o: %.S rule in
# the root Makefile would otherwise lose the explicit hello dependency.
arch/arm64/sources/hello_embed.o: arch/arm64/sources/hello_embed.S $(MUSL_AARCH64_HELLO)
	@echo "  AS      $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Minimal ARM64 boot image (identity map — full ARCH_OBJS need freestanding libc).
# Board: ARM64_BOARD=qemu-virt|rpi4|rpi5 (see scripts/make/arm64-board.mk).
ARM64_BOOT_CFLAGS = -ffreestanding -nostdlib -fno-builtin -O2 -mgeneral-regs-only \
	-DIR0_FREESTANDING_BOOT=1 $(ARM64_BOARD_CFLAGS) \
	-I. -Iarch/arm64/include -Iarch/arm64/sources -Iarch/common -Iincludes -Iktm/include -Iincludes/ir0
ARM64_ALL_CFLAGS = $(ARM64_BOOT_CFLAGS) -ffunction-sections -fdata-sections \
	-DCONFIG_ENABLE_FS_MINIX=0 -DCONFIG_ENABLE_FS_SIMPLEFS=0 \
	-DCONFIG_ENABLE_FS_FAT16=0 -DCONFIG_ENABLE_FS_EXT2=0
ARM64_ALL_KEEP_SYMBOLS = \
	--undefined=current_process \
	--undefined=process_get_pid \
	--undefined=process_find_by_pid \
	--undefined=process_reparent_children \
	--undefined=process_has_zombie_child \
	--undefined=process_child_wait_status_word \
	--undefined=process_wait_child_matches_blocked_target \
	--undefined=process_wait_prepare_child_wake \
	--undefined=process_wait_wake_blocked_parent \
	--undefined=process_lifecycle_prepare_reap \
	--undefined=process_reap_detach_zombie \
	--undefined=process_wait_state_arm \
	--undefined=fork_prepare_child_return \
	--undefined=process_saved_environ_clone \
	--undefined=process_set_tls \
	--undefined=mm_create \
	--undefined=process_mm_create_root \
	--undefined=spawn \
	--undefined=process_init_fd_table \
	--undefined=fork \
	--undefined=exec_detach_shared_mm \
	--undefined=fd_resource_acquire \
	--undefined=process_exit \
	--undefined=process_pre_zombie_teardown \
	--undefined=process_notify_parent_of_exit \
	--undefined=process_resources_release_on_exit \
	--undefined=process_destroy \
	--undefined=process_exit_guest \
	--undefined=process_guest_install \
	--undefined=kernel_idle_loop \
	--undefined=process_idle_bind \
	--undefined=vfs_open \
	--undefined=vfs_read \
	--undefined=vfs_write \
	--undefined=tmpfs_register
ARM64_BOOT_ASFLAGS = -ffreestanding -nostdlib -mgeneral-regs-only
# QEMU virt: pin GICv2 (default may be v3 — freestanding Dist/CPU iface is v2).
ARM64_QEMU_MACHINE = virt,gic-version=2
# F7b curated portable objs (not ALL_OBJS).
ARM64_SLICE_OBJS = \
	arch/arm64/sources/slice_hello.o \
	arch/arm64/sources/board.o \
	arch/arm64/sources/boot_info.o \
	arch/arm64/sources/pl011.o \
	arch/arm64/sources/serial_io_arm64.o
ARM64_PORTABLE_OBJS = \
	$(ARM64_SLICE_OBJS) \
	arch/arm64/sources/gic_v2.o \
	arch/arm64/sources/bcm2836_irq.o \
	arch/arm64/sources/irq_backend.o \
	arch/arm64/sources/arch_irq_init.o \
	arch/arm64/sources/interrupts.o \
	arch/arm64/sources/timer.o \
	arch/arm64/sources/portable_string.o

# Single virt-boot link inventory. rpi3/min/all derive from this so a new
# early object is added once.
ARM64_BOOT_LINK_OBJS = \
	arch/arm64/sources/boot_entry.o \
	arch/arm64/sources/boot_stub.o \
	arch/arm64/sources/mmu_early.o \
	arch/arm64/sources/exc_early.o \
	arch/arm64/sources/pl011.o \
	arch/arm64/sources/board.o \
	arch/arm64/sources/boot_info.o \
	arch/arm64/sources/platform.o \
	arch/arm64/sources/freestanding_stubs.o \
	arch/arm64/sources/serial_io_arm64.o \
	arch/arm64/sources/slice_hello.o \
	build/arm64-boot/boot_log.o \
	arch/arm64/sources/timer.o \
	arch/arm64/sources/gic_v2.o \
	arch/arm64/sources/bcm2836_irq.o \
	arch/arm64/sources/irq_backend.o \
	arch/arm64/sources/syscall_decode.o \
	arch/arm64/sources/elf_policy.o \
	build/arm64-boot/syscall_table.o \
	build/arm64-boot/early_dispatch.o \
	build/arm64-boot/early_time.o \
	build/arm64-boot/early_io.o \
	build/arm64-boot/early_vfs.o \
	build/arm64-boot/early_rootfs.o \
	build/arm64-boot/ramtab.o \
	build/arm64-boot/early_anonfd.o \
	build/arm64-boot/early_unix.o \
	build/arm64-boot/product_ipc.o \
	build/arm64-boot/vfs_dispatch.o \
	build/arm64-boot/early_elf_stack.o \
	build/arm64-boot/early_process.o \
	build/arm64-boot/early_mm.o \
	arch/arm64/sources/syscall_early.o \
	arch/arm64/sources/mm_ops.o \
	arch/arm64/sources/switch_early.o \
	arch/arm64/sources/switch_early_asm.o \
	arch/arm64/sources/process_early.o \
	build/arm64-boot/switch_arm64.o \
	build/arm64-boot/arch_context_switch.o \
	build/arm64-boot/arch_switch.o \
	build/arm64-boot/arch_tls.o \
	build/arm64-boot/first_switch.o \
	build/arm64-boot/portable_string.o \
	build/arm64-boot/sched.o \
	build/arm64-boot/runqueue_lock.o \
	build/arm64-boot/sched_switch.o \
	build/arm64-boot/rr_sched.o \
	build/arm64-boot/process_registry.o \
	build/arm64-boot/process_registry_lock.o \
	build/arm64-boot/cpu_topology.o \
	build/arm64-boot/percpu.o \
	arch/arm64/sources/rr_early.o \
	build/arm64-boot/allocator_stubs.o \
	build/arm64-boot/clock_accounting.o \
	build/arm64-boot/sched_trace.o \
	build/arm64-boot/elf64_image.o \
	arch/arm64/sources/elf_image_early.o \
	arch/arm64/sources/elf_load_early.o \
	arch/arm64/sources/hello_embed.o \
	arch/arm64/sources/embedded_elf_early.o \
	arch/arm64/sources/rootfs_early.o \
	arch/arm64/sources/init_handoff_early.o \
	build/arm64-boot/init_handoff.o \
	arch/arm64/sources/embedded_elf_embed.o \
	drivers/virtio/virtio_mmio.o \
	build/arm64-boot/blockdev.o \
	arch/arm64/sources/virtio_blk_early.o \
	arch/arm64/sources/virtio_net_early.o \
	arch/arm64/sources/vectors.o

ARM64_RPI3_LINK_OBJS := $(ARM64_BOOT_LINK_OBJS)
ARM64_RPI3_LINK_OBJS := $(subst arch/arm64/sources/boot_stub.o,build/arm64-rpi3-early/boot_stub.o,$(ARM64_RPI3_LINK_OBJS))
ARM64_RPI3_LINK_OBJS := $(subst arch/arm64/sources/board.o,build/arm64-rpi3-early/board.o,$(ARM64_RPI3_LINK_OBJS))
ARM64_RPI3_LINK_OBJS := $(subst arch/arm64/sources/vectors.o,build/arm64-rpi3-early/vectors.o,$(ARM64_RPI3_LINK_OBJS))
ARM64_RPI3_LINK_OBJS := $(subst arch/arm64/sources/syscall_early.o,build/arm64-rpi3-early/syscall_early.o,$(ARM64_RPI3_LINK_OBJS))
ARM64_RPI3_LINK_OBJS := $(subst arch/arm64/sources/elf_load_early.o,build/arm64-rpi3-early/elf_load_early.o,$(ARM64_RPI3_LINK_OBJS))
ARM64_RPI3_LINK_OBJS := $(subst arch/arm64/sources/hello_embed.o,build/arm64-rpi3-early/hello_embed.o,$(ARM64_RPI3_LINK_OBJS))
ARM64_RPI3_LINK_OBJS := $(subst arch/arm64/sources/embedded_elf_early.o,build/arm64-rpi3-early/embedded_elf_early.o,$(ARM64_RPI3_LINK_OBJS))
ARM64_RPI3_LINK_OBJS := $(subst arch/arm64/sources/embedded_elf_embed.o,build/arm64-rpi3-early/embedded_elf_embed.o,$(ARM64_RPI3_LINK_OBJS))

ARM64_MIN_LINK_OBJS = \
	$(filter-out build/arm64-boot/portable_string.o \
		build/arm64-boot/allocator_stubs.o,$(ARM64_BOOT_LINK_OBJS)) \
	build/arm64-min/arch_mm.o \
	build/arm64-min/portable_string.o \
	build/arm64-min/allocator.o \
	build/arm64-min/boot_memory.o \
	build/arm64-min/no_mmu.o \
	build/arm64-min/paging.o \
	build/arm64-min/pmm.o \
	build/arm64-min/kmem.o \
	build/arm64-min/tlb_sync.o \
	build/arm64-min/errno.o \
	build/arm64-min/open_flags.o \
	build/arm64-min/min_link_stubs.o

ARM64_ALL_EARLY_LINK_OBJS = \
	$(filter-out build/arm64-boot/portable_string.o \
		build/arm64-boot/allocator_stubs.o \
		build/arm64-boot/cpu_topology.o \
		build/arm64-boot/percpu.o \
		build/arm64-boot/runqueue_lock.o \
		build/arm64-boot/process_registry_lock.o \
		build/arm64-boot/arch_tls.o,$(ARM64_BOOT_LINK_OBJS))

.PHONY: kernel-arm64-boot.bin kernel-arm64-mmu.bin kernel-arm64-vbar.bin \
	kernel-arm64-el0.bin kernel-arm64-slice.bin kernel-arm64-port.bin \
	kernel-arm64-gic.bin kernel-arm64-syscall.bin \
	arm64-slice-compile arm64-portable-compile arm64-all-objs-probe
arm64-slice-compile:
	@echo "  CC      ARM64_SLICE_OBJS (F7b)"
	@for f in $(ARM64_SLICE_OBJS); do \
		src=$${f%.o}.c; \
		echo "  CC      $$src"; \
		aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c $$src -o $$f || exit 1; \
	done
	@echo "  CC      includes/string.c (compile-only probe, not linked — needs oops.h)"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) \
		-c includes/string.c -o $(HOST_TMP)/ir0-string-arm64-probe.o
	@echo "✓ arm64-slice-compile OK (slice+pl011+serial_io + string.c probe)"

arm64-portable-compile: arm64-slice-compile
	@echo "  CC      ARM64_PORTABLE_OBJS (F7b P2 — not ALL_OBJS)"
	@for f in $(ARM64_PORTABLE_OBJS); do \
		src=$${f%.o}.c; \
		echo "  CC      $$src"; \
		aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c $$src -o $$f || exit 1; \
	done
	@echo "  CC      arch/arm64/sources/irq_portable_stubs.c (INTERRUPT_OBJS_ARM64)"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/irq_portable_stubs.c \
		-o arch/arm64/sources/irq_portable_stubs.o
	@echo "✓ arm64-portable-compile OK (slice+pl011+serial+gic+timer+portable_string+irq stubs)"

# Pack E: probe MEMORY_OBJS for ARCH=arm64 (compile-only). Not a full ALL_OBJS link.
arm64-all-objs-probe: arm64-portable-compile
	@echo "  PROBE   arm64-all-objs-probe (MEMORY_OBJS compile-only)"
	@rm -f $(HOST_TMP)/ir0-arm64-all-objs-probe.log
	@echo "INTERRUPT_OBJS wall: cleared (irq_portable_stubs replaces lidt/isr_stubs_64)" \
		> $(HOST_TMP)/ir0-arm64-all-objs-probe.log
	@fail=0; \
	for src in mm/allocator.c mm/boot_memory.c mm/no_mmu.c mm/paging.c mm/pmm.c mm/kmem.c; do \
		echo "  CC      $$src (probe)"; \
		if ! aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
			-c $$src -o $(HOST_TMP)/ir0-arm64-probe-$$$$.o >> $(HOST_TMP)/ir0-arm64-all-objs-probe.log 2>&1; then \
			echo "FIRST_DIVERGENCE: $$src" | tee -a $(HOST_TMP)/ir0-arm64-all-objs-probe.log; \
			fail=1; \
			break; \
		fi; \
		rm -f $(HOST_TMP)/ir0-arm64-probe-$$$$.o; \
	done; \
	if [ $$fail -eq 0 ]; then \
		for src in kernel/main.c kernel/lib/open_flags.c \
			kernel/errno.c kernel/credentials.c \
			sched/switch/switch_arm64.c \
			kernel/clock_wait.c kernel/driver_registry.c \
			kernel/lib/copy_user.c \
			kernel/lib/logging.c kernel/futex.c \
			kernel/net_compat.c; do \
			echo "  CC      $$src (KERNEL probe)"; \
			if ! aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
				-I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 \
				-I$(KERNEL_ROOT)/arch/common \
				-I$(KERNEL_ROOT)/fs -I$(KERNEL_ROOT)/net \
				-I$(KERNEL_ROOT)/sched -I$(KERNEL_ROOT) \
				-c $$src -o $(HOST_TMP)/ir0-arm64-probe-$$$$.o >> $(HOST_TMP)/ir0-arm64-all-objs-probe.log 2>&1; then \
				echo "FIRST_DIVERGENCE: $$src" | tee -a $(HOST_TMP)/ir0-arm64-all-objs-probe.log; \
				fail=1; \
				break; \
			fi; \
			rm -f $(HOST_TMP)/ir0-arm64-probe-$$$$.o; \
		done; \
	fi; \
	if [ $$fail -eq 0 ]; then \
		echo "MEMORY_OBJS+KERNEL sample(+errno/cred/switch/clock/drv/copy_user/logging/futex/net_compat): compile OK (not linked)" \
			| tee -a $(HOST_TMP)/ir0-arm64-all-objs-probe.log; \
		echo "NEXT: make ARCH=arm64 kernel-arm64-all.bin (portable ALL link; x86 drivers still out)"; \
		echo "✓ arm64-all-objs-probe: interrupt wall cleared; probe compile OK"; \
	else \
		echo "✗ arm64-all-objs-probe: see $(HOST_TMP)/ir0-arm64-all-objs-probe.log"; \
		tail -40 $(HOST_TMP)/ir0-arm64-all-objs-probe.log; \
		exit 1; \
	fi

kernel-arm64-boot.bin: arch/arm64/sources/boot_stub.c arch/arm64/sources/mmu_early.c \
		arch/arm64/asm/boot/entry.S arch/arm64/sources/boot_info.c \
		arch/arm64/sources/mmu_early.h arch/arm64/sources/exc_early.c \
		arch/arm64/sources/exc_early.h arch/arm64/sources/slice_hello.c \
		arch/arm64/sources/slice_hello.h arch/arm64/sources/pl011.c \
		arch/arm64/sources/pl011.h arch/arm64/sources/serial_io_arm64.c \
		arch/arm64/sources/timer.c arch/arm64/sources/timer.h \
		arch/arm64/sources/gic_v2.c arch/arm64/sources/gic_v2.h \
		arch/arm64/sources/bcm2836_irq.c arch/arm64/sources/bcm2836_irq.h \
		arch/arm64/sources/irq_backend.c arch/arm64/sources/irq_backend.h \
		arch/arm64/sources/syscall_decode.c includes/ir0/syscall_id.h \
		kernel/lib/syscall_table.c includes/ir0/syscall_table.h \
		kernel/syscalls/early_dispatch.c includes/ir0/early_syscall.h \
		kernel/syscalls/early_time.c includes/ir0/early_time.h \
		kernel/syscalls/early_io.c includes/ir0/early_io.h \
		kernel/syscalls/early_vfs.c includes/ir0/early_vfs.h \
		kernel/syscalls/early_rootfs.c includes/ir0/early_rootfs.h \
		fs/ramtab.c includes/ir0/ramtab.h includes/ir0/product_path.h \
		ipc/early_anonfd.c includes/ir0/early_anonfd.h \
		ipc/early_unix.c includes/ir0/early_unix.h \
		ipc/product_ipc.c includes/ir0/product_ipc.h \
		fs/vfs_dispatch.c includes/ir0/vfs_dispatch.h \
		kernel/lib/early_elf_stack.c includes/ir0/early_elf_stack.h \
		kernel/syscalls/early_process.c includes/ir0/early_process.h \
		includes/ir0/clone.h \
		kernel/syscalls/early_mm.c includes/ir0/early_mm.h \
		arch/arm64/sources/elf_policy.c includes/ir0/arch_elf.h \
		arch/arm64/sources/syscall_early.c arch/arm64/sources/syscall_early.h \
		arch/arm64/sources/mm_ops.c arch/arm64/asm/entry/vectors.S \
		arch/arm64/sources/switch_early.c arch/arm64/sources/switch_early.h \
		arch/arm64/asm/context/switch_early.S \
		arch/arm64/sources/process_early.c arch/arm64/sources/process_early.h \
		arch/arm64/sources/elf_load_early.c arch/arm64/sources/elf_load_early.h \
		arch/arm64/sources/hello_embed.S \
		arch/arm64/sources/embedded_elf_embed.S \
		arch/arm64/sources/embedded_elf_early.c \
		arch/arm64/sources/rootfs_early.c arch/arm64/sources/rootfs_early.h \
		arch/arm64/sources/rr_early.c arch/arm64/sources/rr_early.h \
		ktm/mocks/arm64/allocator_stubs.c \
		kernel/clock_accounting.c kernel/process/sched_trace.c \
		arch/arm64/sources/virtio_blk_early.c arch/arm64/sources/virtio_blk_early.h \
		arch/arm64/sources/virtio_net_early.c arch/arm64/sources/virtio_net_early.h \
		drivers/virtio/virtio_mmio.c includes/ir0/virtio_mmio.h \
		kernel/lib/blockdev.c includes/ir0/blockdev.h \
		sched/sched.c sched/runqueue_lock.c sched/sched_switch.c sched/rr_sched.c \
		kernel/process/registry_lock.c \
		kernel/cpu_topology.c kernel/percpu.c \
		sched/switch/switch_arm64.c sched/task.h \
		arch/arm64/linker.ld $(MUSL_AARCH64_HELLO)
	@mkdir -p build/arm64-boot
	@echo "  CC      arch/arm64/sources/boot_stub.c (F7j process+TTBR+musl+RR+bb+virtio)"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -c \
		arch/arm64/sources/boot_stub.c -o arch/arm64/sources/boot_stub.o
	@echo "  CC      arch/arm64/sources/mmu_early.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/mmu_early.c -o arch/arm64/sources/mmu_early.o
	@echo "  CC      arch/arm64/sources/exc_early.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/exc_early.c -o arch/arm64/sources/exc_early.o
	@echo "  CC      arch/arm64/sources/pl011.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/pl011.c -o arch/arm64/sources/pl011.o
	@echo "  CC      arch/arm64/sources/board.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/board.c -o arch/arm64/sources/board.o
	@echo "  CC      arch/arm64/sources/boot_info.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/boot_info.c -o arch/arm64/sources/boot_info.o
	@echo "  CC      arch/arm64/sources/platform.c (board power ops)"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/platform.c -o arch/arm64/sources/platform.o
	@echo "  CC      arch/arm64/sources/freestanding_stubs.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/freestanding_stubs.c -o arch/arm64/sources/freestanding_stubs.o
	@echo "  CC      arch/arm64/sources/serial_io_arm64.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/serial_io_arm64.c -o arch/arm64/sources/serial_io_arm64.o
	@echo "  CC      arch/arm64/sources/slice_hello.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/slice_hello.c -o arch/arm64/sources/slice_hello.o
	@echo "  CC      arch/arm64/sources/timer.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/timer.c -o arch/arm64/sources/timer.o
	@echo "  CC      arch/arm64/sources/gic_v2.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/gic_v2.c -o arch/arm64/sources/gic_v2.o
	@echo "  CC      arch/arm64/sources/bcm2836_irq.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/bcm2836_irq.c -o arch/arm64/sources/bcm2836_irq.o
	@echo "  CC      arch/arm64/sources/irq_backend.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/irq_backend.c -o arch/arm64/sources/irq_backend.o
	@echo "  CC      arch/arm64/sources/syscall_early.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/syscall_early.c -o arch/arm64/sources/syscall_early.o
	@echo "  CC      arch/arm64/sources/syscall_decode.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/syscall_decode.c -o arch/arm64/sources/syscall_decode.o
	@echo "  CC      kernel/lib/syscall_table.c (semantic dispatch)"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -c kernel/lib/syscall_table.c \
		-o build/arm64-boot/syscall_table.o
	@echo "  CC      kernel/syscalls/early_dispatch.c (staged semantic dispatch)"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -c kernel/syscalls/early_dispatch.c \
		-o build/arm64-boot/early_dispatch.o
	@echo "  CC      kernel/syscalls/early_time.c (staged POSIX time)"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -c kernel/syscalls/early_time.c \
		-o build/arm64-boot/early_time.o
	@echo "  CC      kernel/syscalls/early_io.c (staged descriptor semantics)"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -c kernel/syscalls/early_io.c \
		-o build/arm64-boot/early_io.o
	@echo "  CC      kernel/syscalls/early_vfs.c (staged VFS routing)"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -c kernel/syscalls/early_vfs.c \
		-o build/arm64-boot/early_vfs.o
	@echo "  CC      kernel/syscalls/early_rootfs.c (staged / and /init)"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -c kernel/syscalls/early_rootfs.c \
		-o build/arm64-boot/early_rootfs.o
	@echo "  CC      fs/ramtab.c (path table for staged names)"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -c fs/ramtab.c \
		-o build/arm64-boot/ramtab.o
	@echo "  CC      ipc/early_anonfd.c (staged anon fd tables)"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -c ipc/early_anonfd.c \
		-o build/arm64-boot/early_anonfd.o
	@echo "  CC      ipc/early_unix.c (staged AF_UNIX tables)"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -c ipc/early_unix.c \
		-o build/arm64-boot/early_unix.o
	@echo "  CC      ipc/product_ipc.c (product PIPE2/SOCKETPAIR)"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -c ipc/product_ipc.c \
		-o build/arm64-boot/product_ipc.o
	@echo "  CC      fs/vfs_dispatch.c (portable vfs_ops dispatch)"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -c fs/vfs_dispatch.c \
		-o build/arm64-boot/vfs_dispatch.o
	@echo "  CC      kernel/lib/early_elf_stack.c (staged SysV ELF stack)"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -c kernel/lib/early_elf_stack.c \
		-o build/arm64-boot/early_elf_stack.o
	@echo "  CC      kernel/syscalls/early_process.c (staged Linux process ABI)"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -c kernel/syscalls/early_process.c \
		-o build/arm64-boot/early_process.o
	@echo "  CC      kernel/syscalls/early_mm.c (staged bounded MM policy)"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -c kernel/syscalls/early_mm.c \
		-o build/arm64-boot/early_mm.o
	@echo "  CC      arch/arm64/sources/elf_policy.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/elf_policy.c -o arch/arm64/sources/elf_policy.o
	@echo "  CC      arch/arm64/sources/mm_ops.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/mm_ops.c -o arch/arm64/sources/mm_ops.o
	@echo "  CC      arch/arm64/sources/switch_early.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/switch_early.c -o arch/arm64/sources/switch_early.o
	@echo "  CC      arch/arm64/sources/process_early.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/sched \
		-I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 \
		-c arch/arm64/sources/process_early.c -o arch/arm64/sources/process_early.o
	@echo "  CC      sched/rr_sched.c"
	@echo "  CC      kernel/process/registry.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
		-I$(KERNEL_ROOT)/sched -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -I$(KERNEL_ROOT)/arch/common \
		-I$(KERNEL_ROOT) \
		-c kernel/process/registry.c -o build/arm64-boot/process_registry.o
	@echo "  CC      kernel/process/registry_lock.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
		-I$(KERNEL_ROOT)/sched -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -I$(KERNEL_ROOT) \
		-c kernel/process/registry_lock.c \
		-o build/arm64-boot/process_registry_lock.o
	@echo "  CC      kernel/cpu_topology.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
		-I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 \
		-c kernel/cpu_topology.c -o build/arm64-boot/cpu_topology.o
	@echo "  CC      kernel/percpu.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
		-I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 \
		-c kernel/percpu.c -o build/arm64-boot/percpu.o
	@echo "  CC      sched/sched.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
		-DIR0_SCHED_EARLY_RR=1 \
		-I$(KERNEL_ROOT)/sched -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -I$(KERNEL_ROOT)/arch/common \
		-I$(KERNEL_ROOT) \
		-c sched/sched.c -o build/arm64-boot/sched.o
	@echo "  CC      sched/runqueue_lock.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
		-I$(KERNEL_ROOT)/sched -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -I$(KERNEL_ROOT) \
		-c sched/runqueue_lock.c -o build/arm64-boot/runqueue_lock.o
	@echo "  CC      sched/sched_switch.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
		-I$(KERNEL_ROOT)/sched -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -I$(KERNEL_ROOT)/arch/common \
		-I$(KERNEL_ROOT) \
		-c sched/sched_switch.c -o build/arm64-boot/sched_switch.o
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
		-I$(KERNEL_ROOT)/sched -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -I$(KERNEL_ROOT)/arch/common \
		-I$(KERNEL_ROOT) \
		-c sched/rr_sched.c -o build/arm64-boot/rr_sched.o
	@echo "  CC      sched/switch/switch_arm64.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/sched \
		-I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 \
		-I$(KERNEL_ROOT)/arch/common \
		-c sched/switch/switch_arm64.c -o build/arm64-boot/switch_arm64.o
	@echo "  CC      sched/switch/arch_context_switch.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
		-I$(KERNEL_ROOT)/sched -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -I$(KERNEL_ROOT)/arch/common \
		-I$(KERNEL_ROOT) \
		-c sched/switch/arch_context_switch.c -o build/arm64-boot/arch_context_switch.o
	@echo "  CC      ARM64 context providers"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
		-I$(KERNEL_ROOT)/sched -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -I$(KERNEL_ROOT)/arch/common \
		-I$(KERNEL_ROOT) \
		-c arch/arm64/sources/arch_switch.c -o build/arm64-boot/arch_switch.o
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
		-I$(KERNEL_ROOT)/sched -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -I$(KERNEL_ROOT)/arch/common \
		-I$(KERNEL_ROOT) \
		-c arch/arm64/sources/arch_tls.c -o build/arm64-boot/arch_tls.o
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
		-I$(KERNEL_ROOT)/sched -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -I$(KERNEL_ROOT)/arch/common \
		-I$(KERNEL_ROOT) \
		-c arch/arm64/sources/first_switch.c -o build/arm64-boot/first_switch.o
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) \
		-c arch/arm64/sources/portable_string.c \
		-o build/arm64-boot/portable_string.o
	@echo "  CC      arch/arm64/sources/rr_early.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
		-I$(KERNEL_ROOT)/sched -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -I$(KERNEL_ROOT)/arch/common \
		-I$(KERNEL_ROOT) \
		-c arch/arm64/sources/rr_early.c -o arch/arm64/sources/rr_early.o
	@echo "  CC      ARM64 staged allocator provider"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
		-I$(KERNEL_ROOT)/sched -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -I$(KERNEL_ROOT)/arch/common \
		-I$(KERNEL_ROOT) \
		-c ktm/mocks/arm64/allocator_stubs.c \
		-o build/arm64-boot/allocator_stubs.o
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
		-I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 \
		-c kernel/clock_accounting.c \
		-o build/arm64-boot/clock_accounting.o
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
		-I$(KERNEL_ROOT)/sched -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -I$(KERNEL_ROOT)/arch/common \
		-I$(KERNEL_ROOT) -c kernel/process/sched_trace.c \
		-o build/arm64-boot/sched_trace.o
	@echo "  CC      arch/arm64/sources/elf_load_early.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		kernel/lib/elf64_image.c -o build/arm64-boot/elf64_image.o
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/elf_image_early.c -o arch/arm64/sources/elf_image_early.o
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/elf_load_early.c -o arch/arm64/sources/elf_load_early.o
	@echo "  CC      arch/arm64/sources/embedded_elf_early.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/embedded_elf_early.c -o arch/arm64/sources/embedded_elf_early.o
	@echo "  CC      common init handoff + ARM64 provider"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		kernel/init_handoff.c -o build/arm64-boot/init_handoff.o
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/init_handoff_early.c -o arch/arm64/sources/init_handoff_early.o
	@echo "  CC      arch/arm64/sources/rootfs_early.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/rootfs_early.c -o arch/arm64/sources/rootfs_early.o
	@echo "  CC      drivers/virtio/virtio_mmio.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -c \
		drivers/virtio/virtio_mmio.c -o drivers/virtio/virtio_mmio.o
	@echo "  CC      kernel/lib/blockdev.c (portable facade)"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -c \
		kernel/lib/blockdev.c -o build/arm64-boot/blockdev.o
	@echo "  CC      arch/arm64/sources/virtio_blk_early.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -c \
		arch/arm64/sources/virtio_blk_early.c -o arch/arm64/sources/virtio_blk_early.o
	@echo "  CC      arch/arm64/sources/virtio_net_early.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -I$(KERNEL_ROOT)/includes \
		-I$(KERNEL_ROOT)/includes/ir0 -c \
		arch/arm64/sources/virtio_net_early.c -o arch/arm64/sources/virtio_net_early.o
	@echo "  AS      arch/arm64/sources/hello_embed.S"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_ASFLAGS) -c \
		arch/arm64/sources/hello_embed.S -o arch/arm64/sources/hello_embed.o
	@echo "  AS      arch/arm64/sources/embedded_elf_embed.S"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_ASFLAGS) \
		-DARM64_EMBEDDED_ELF_PATH='"build/hello_aarch64"' -c \
		arch/arm64/sources/embedded_elf_embed.S -o arch/arm64/sources/embedded_elf_embed.o
	@echo "  AS      arch/arm64/asm/entry/vectors.S"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_ASFLAGS) -c \
		arch/arm64/asm/entry/vectors.S -o arch/arm64/sources/vectors.o
	@echo "  AS      arch/arm64/asm/boot/entry.S"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_ASFLAGS) -c \
		arch/arm64/asm/boot/entry.S -o arch/arm64/sources/boot_entry.o
	@echo "  AS      arch/arm64/asm/context/switch_early.S"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_ASFLAGS) -c \
		arch/arm64/asm/context/switch_early.S -o arch/arm64/sources/switch_early_asm.o
	@echo "  CC      arch/common/boot_log.c (freestanding)"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/common/boot_log.c -o build/arm64-boot/boot_log.o
	@echo "  LD      $@"
	@aarch64-linux-gnu-ld -T arch/arm64/linker.ld -o $@ $(ARM64_BOOT_LINK_OBJS)
	@echo "✓ $@"


.PHONY: kernel-arm64-Image
kernel-arm64-Image: kernel-arm64-boot.bin
	@aarch64-linux-gnu-objcopy -O binary kernel-arm64-boot.bin $@
	@echo "✓ $@ (Linux arm64 Image header)"

# Alias: same image; smoke looks for post-MMU / VBAR / EL0 / slice / port tags.
kernel-arm64-mmu.bin: kernel-arm64-boot.bin
	@cp -f kernel-arm64-boot.bin $@
	@echo "✓ $@"

kernel-arm64-vbar.bin: kernel-arm64-boot.bin
	@cp -f kernel-arm64-boot.bin $@
	@echo "✓ $@"

kernel-arm64-el0.bin: kernel-arm64-boot.bin
	@cp -f kernel-arm64-boot.bin $@
	@echo "✓ $@"

kernel-arm64-slice.bin: kernel-arm64-boot.bin
	@cp -f kernel-arm64-boot.bin $@
	@echo "✓ $@"

kernel-arm64-port.bin: kernel-arm64-boot.bin
	@cp -f kernel-arm64-boot.bin $@
	@echo "✓ $@"

kernel-arm64-gic.bin: kernel-arm64-boot.bin
	@cp -f kernel-arm64-boot.bin $@
	@echo "✓ $@"

kernel-arm64-syscall.bin: kernel-arm64-boot.bin
	@cp -f kernel-arm64-boot.bin $@
	@echo "✓ $@"

# Honest min link: MEMORY_OBJS + KERNEL sample + freestanding ARCH (+ stubs).
# Does NOT claim product ALL_OBJS (x86 ATA/RTL8139/VBE remain excluded).
.PHONY: kernel-arm64-min.bin
kernel-arm64-min.bin: kernel-arm64-boot.bin ktm/mocks/arm64/min_link_stubs.c \
		arch/arm64/sources/freestanding_stubs.c
	@echo "  CC      ARM64_MIN MEMORY+KERNEL sample"
	@mkdir -p build/arm64-min
	@fail=0; \
	for src in mm/allocator.c mm/boot_memory.c mm/no_mmu.c mm/paging.c mm/pmm.c mm/kmem.c \
		mm/tlb_sync.c \
		kernel/errno.c kernel/lib/open_flags.c \
		arch/arm64/sources/arch_mm.c arch/arm64/sources/portable_string.c; do \
		base=$$(basename $$src .c); \
		echo "  CC      $$src (min)"; \
		if ! aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
			-I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 \
			-I$(KERNEL_ROOT)/arch/common \
			-I$(KERNEL_ROOT)/fs -I$(KERNEL_ROOT)/net \
			-I$(KERNEL_ROOT)/sched -I$(KERNEL_ROOT) \
			-c $$src -o build/arm64-min/$$base.o; then \
			echo "✗ compile $$src"; fail=1; break; \
		fi; \
	done; \
	if [ $$fail -ne 0 ]; then exit 1; fi
	@echo "  CC      ktm/mocks/arm64/min_link_stubs.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		ktm/mocks/arm64/min_link_stubs.c -o build/arm64-min/min_link_stubs.o
	@echo "  LD      $@ (boot + MEMORY sample — not ALL_OBJS)"
	@aarch64-linux-gnu-ld -T arch/arm64/linker.ld -o $@ \
		$(ARM64_MIN_LINK_OBJS) \
		--defsym ARM64_MIN_LINK_MARKER=1
	@echo "✓ $@ (MEMORY+KERNEL sample+ARCH freestanding linked; drivers ALL_OBJS still BLOCKED)"

# The aggregate boot image is still an early-runtime composition.  Keep its
# historical filename for automation compatibility, but do not confuse it with
# the product KERNEL_OBJS graph.  The compile contract below covers the entire
# common process subsystem independently of that early link boundary.
ARM64_COMMON_PROCESS_SRCS = \
	kernel/process/registry.c \
	kernel/process/core.c \
	kernel/process/create.c \
	kernel/process/domains.c \
	kernel/process/mm_struct.c \
	kernel/process/files_struct.c \
	kernel/process/fork.c \
	kernel/process/exec.c \
	kernel/process/lifecycle.c \
	kernel/process/parent.c \
	kernel/process/resources.c \
	kernel/process/exit.c \
	kernel/process/wait.c \
	kernel/process/wait_audit.c \
	kernel/process/idle.c \
	kernel/process/wait_state.c \
	kernel/process/saved_context.c \
	kernel/process/saved_environ.c \
	kernel/process/signal_enter.c \
	kernel/process/fdtable.c \
	kernel/process/pseudo_fd_bind.c \
	kernel/process/mm.c \
	kernel/process/signals.c

.PHONY: arm64-common-process-compile
arm64-common-process-compile:
	@echo "  CC      ARM64 common process subsystem"
	@mkdir -p build/arm64-common-process
	@set -e; for src in $(ARM64_COMMON_PROCESS_SRCS); do \
		obj=build/arm64-common-process/$$(basename $$src .c).o; \
		echo "  CC      $$src"; \
		aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
			-I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 \
			-I$(KERNEL_ROOT)/arch/common \
			-I$(KERNEL_ROOT)/fs -I$(KERNEL_ROOT)/net \
			-I$(KERNEL_ROOT)/sched -I$(KERNEL_ROOT) \
			-c $$src -o $$obj; \
	done
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -DARCH_ARM64=1 \
		-I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 \
		-c arch/arm64/sources/arch_debug.c \
		-o build/arm64-common-process/arch_debug.o
	@echo "✓ ARM64 common process subsystem compile contract"

.PHONY: kernel-arm64-all.bin kernel-arm64-all-Image \
	arm64-common-core-compile arm64-provider-audit

# Compile every architecture-neutral kernel/MM/library/VFS translation unit in
# an ARM64-only object namespace.  This is deliberately broader than the early
# aggregate link below and prevents a previous x86 build from satisfying an ARM
# prerequisite with an incompatible in-tree object.  Drivers remain outside
# this set until the driver core can select them by bus/platform capabilities.
ARM64_COMMON_CORE_INPUT_OBJS = \
	$(KERNEL_OBJS) $(MEMORY_OBJS) $(LIB_OBJS) $(FS_OBJS)
ARM64_COMMON_CORE_C_OBJS = $(filter %.o,$(filter-out \
	arch/% tests/% ktm/%,$(ARM64_COMMON_CORE_INPUT_OBJS)))
ARM64_COMMON_CORE_SRCS = $(ARM64_COMMON_CORE_C_OBJS:.o=.c)
ARM64_COMMON_CORE_BUILD_OBJS = $(addprefix build/arm64-common-core/,$(ARM64_COMMON_CORE_C_OBJS))

build/arm64-common-core/%.o: %.c
	@mkdir -p $(@D)
	@echo "  CC      $< (ARM64 common core)"
	@aarch64-linux-gnu-gcc $(ARM64_ALL_CFLAGS) -DARCH_ARM64=1 \
		-I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 \
		-I$(KERNEL_ROOT)/arch/common -I$(KERNEL_ROOT)/fs \
		-I$(KERNEL_ROOT)/net -I$(KERNEL_ROOT)/sched -I$(KERNEL_ROOT) \
		-c $< -o $@

arm64-common-core-compile: $(ARM64_COMMON_CORE_BUILD_OBJS)
	@test $(words $(ARM64_COMMON_CORE_SRCS)) -gt 80 || \
		{ echo "✗ ARM64 common-core inventory unexpectedly small"; exit 1; }
	@! printf '%s\n' $(ARM64_COMMON_CORE_SRCS) | grep -Eq '^arch/|^ktm/'
	@echo "✓ ARM64 common core compile contract ($$(printf '%s\n' $(ARM64_COMMON_CORE_SRCS) | wc -l) C units)"

# One inventory owns both compilation and linkage of the portable aggregate.
# Object basenames are intentionally unique; keep the assertion below so a new
# provider cannot silently overwrite another object in build/arm64-all.
ARM64_ALL_PORTABLE_SRCS = \
	mm/allocator.c \
	mm/boot_memory.c \
	mm/no_mmu.c \
	mm/paging.c \
	mm/tlb_sync.c \
	mm/pmm.c \
	mm/kmem.c \
	kernel/errno.c \
	kernel/cpu_topology.c \
	kernel/percpu.c \
	kernel/lib/open_flags.c \
	kernel/lib/copy_user.c \
	kernel/fd_resource.c \
	sched/runqueue_lock.c \
	kernel/process/registry_lock.c \
	kernel/process/core.c \
	kernel/process/create.c \
	kernel/process/cred_groups.c \
	kernel/process/limits.c \
	kernel/process/mm_struct.c \
	kernel/process/mm.c \
	kernel/process/files_lifecycle.c \
	kernel/process/files_struct.c \
	kernel/process/fdtable_init.c \
	kernel/process/fdtable.c \
	kernel/process/fork.c \
	kernel/process/exec.c \
	kernel/process/lifecycle.c \
	kernel/process/parent.c \
	kernel/process/resources.c \
	kernel/process/exit.c \
	kernel/process/idle.c \
	kernel/syscalls/syscall_identity.c \
	kernel/syscalls/product_vfs.c \
	kernel/process/cwd.c \
	kernel/process/vfs_file.c \
	kernel/credentials.c \
	fs/permissions.c \
	fs/vfs.c \
	fs/tmpfs.c \
	fs/path.c \
	kernel/process/wait.c \
	kernel/process/wait_audit.c \
	kernel/process/signals.c \
	kernel/process/domains.c \
	kernel/process/saved_context.c \
	kernel/process/saved_environ.c \
	kernel/process/pseudo_fd_bind.c \
	kernel/process/signal_enter.c \
	kernel/process/wait_state.c \
	arch/arm64/sources/arch_debug.c \
	arch/arm64/sources/arch_fork.c \
	arch/arm64/sources/arch_task_ops.c \
	arch/arm64/sources/arch_tls.c \
	arch/arm64/sources/task_stack.c \
	arch/arm64/sources/arch_syscall_frame.c \
	arch/arm64/sources/arch_mm.c \
	arch/arm64/sources/portable_string.c
ARM64_ALL_PORTABLE_OBJS = $(addprefix build/arm64-all/,$(notdir $(ARM64_ALL_PORTABLE_SRCS:.c=.o)))

kernel-arm64-all.bin: kernel-arm64-boot.bin \
		arch/arm64/sources/all_objs_mark.c arch/arm64/sources/portable_string.c
	@echo "  CC      ARM64_ALL portable objs (not x86 drivers)"
	@mkdir -p build/arm64-all
	@rm -f $(HOST_TMP)/ir0-arm64-all-link.log
	@test $(words $(ARM64_ALL_PORTABLE_SRCS)) -eq $(words $(sort $(notdir $(ARM64_ALL_PORTABLE_SRCS)))) || \
		{ echo "✗ duplicate ARM64 aggregate object basename"; exit 1; }
	@fail=0; \
	for src in $(ARM64_ALL_PORTABLE_SRCS); do \
		base=$$(basename $$src .c); \
		echo "  CC      $$src (all)"; \
		if ! aarch64-linux-gnu-gcc $(ARM64_ALL_CFLAGS) -DARCH_ARM64=1 \
			-DIR0_KLOG_COMPILE_OUT=1 \
			-I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 \
			-I$(KERNEL_ROOT)/arch/common \
			-I$(KERNEL_ROOT)/fs -I$(KERNEL_ROOT)/net \
			-I$(KERNEL_ROOT)/sched -I$(KERNEL_ROOT) \
			-c $$src -o build/arm64-all/$$base.o 2>>$(HOST_TMP)/ir0-arm64-all-link.log; then \
			echo "✗ compile $$src"; cat $(HOST_TMP)/ir0-arm64-all-link.log | tail -20; exit 1; \
		fi; \
	done
	@echo "  CC      arch/arm64/sources/all_objs_mark.c"
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) -c \
		arch/arm64/sources/all_objs_mark.c -o build/arm64-all/all_objs_mark.o
	@echo "  LD      $@ (portable ALL — no x86 drivers)"
	@aarch64-linux-gnu-ld --gc-sections $(ARM64_ALL_KEEP_SYMBOLS) \
		-Map=build/arm64-all/kernel.map \
		-T arch/arm64/linker.ld -o $@ \
		$(ARM64_ALL_EARLY_LINK_OBJS) \
		$(ARM64_ALL_PORTABLE_OBJS) build/arm64-all/all_objs_mark.o
	@echo "✓ $@ (common process_exit + idle; final EL0 schedules away)"

arm64-provider-audit: kernel-arm64-all.bin
	@semantic=$$(find arch/arm64/sources -maxdepth 1 -type f \
		\( -name 'syscall_*_early.c' -o -name 'syscall_*_early.h' \) \
		! -name 'syscall_early.c' ! -name 'syscall_early.h' -print); \
	if [ -n "$$semantic" ]; then \
		echo "✗ semantic syscall provider leaked into ARM64:"; \
		printf '%s\n' "$$semantic"; exit 1; \
	fi
	@! grep -q 'allocator_stubs.o' build/arm64-all/kernel.map || \
		{ echo "✗ ARM64 aggregate linked the harness allocator"; exit 1; }
	@weak=$$(aarch64-linux-gnu-nm kernel-arm64-all.bin | \
		awk '$$2 ~ /^[WwVv]$$/ { print $$3 }'); \
	if [ -n "$$weak" ]; then \
		echo "✗ weak ARM64 aggregate providers are forbidden:"; \
		printf '%s\n' "$$weak"; exit 1; \
	fi
	@! aarch64-linux-gnu-nm kernel-arm64-all.bin | \
		grep -Eq '[[:space:]](handle_signals|signals_should_handle_on_run|klog_info)$$' || \
		{ echo "✗ unused runtime harness provider leaked into ARM64 aggregate"; exit 1; }
	@echo "✓ ARM64 provider audit (common syscall semantics; no mocks/weak symbols)"

# Keep the aggregate harness on the Linux arm64 boot protocol as well.  QEMU
# supplies x0 only when the payload is an Image, not when it loads the raw ELF
# test binary.  This is intentionally a second test product: it does not
# alter the production firmware handoff or synthesize a DTB address.
kernel-arm64-all-Image: kernel-arm64-all.bin
	@aarch64-linux-gnu-objcopy -O binary kernel-arm64-all.bin $@
