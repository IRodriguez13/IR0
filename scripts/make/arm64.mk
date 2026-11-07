# SPDX-License-Identifier: GPL-3.0-only
#
# ARM64 fragment included by the root Makefile (ARCH=arm64 only).
#
# Products and checks:
#   kernel-arm64.bin / kernel-arm64-product.bin   portable product link ($(ALL_OBJS))
#   kernel-arm64-<board>-min.bin                  tiny board bring-up (scripts/make/arm64-board.mk)
#   arm64-common-core-compile                     compile-only contract for portable TUs
#   arm64-provider-audit                         nm policy on the product ELF
#
# Embedded `hello_aarch64` is built here for EL0 smoke blobs (no ISD required).

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
# a tiny CRT-less ET_EXEC when musl CRT is unavailable (blob-only; product link
# still uses libgcc via the root Makefile).
MUSL_CC_AARCH64 ?= $(shell \
	command -v aarch64-linux-musl-gcc 2>/dev/null || \
	(test -x $(KERNEL_ROOT)/toolchain/aarch64-linux-musl/bin/aarch64-linux-musl-gcc && \
		echo $(KERNEL_ROOT)/toolchain/aarch64-linux-musl/bin/aarch64-linux-musl-gcc) || \
	(test -x $(KERNEL_ROOT)/toolchain/aarch64-linux-musl-cross/bin/aarch64-linux-musl-gcc && \
		echo $(KERNEL_ROOT)/toolchain/aarch64-linux-musl-cross/bin/aarch64-linux-musl-gcc) || \
	true)
MUSL_AARCH64_HELLO := $(KERNEL_ROOT)/build/hello_aarch64

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
		echo "  CC      hello_aarch64 (CRT-less blob — musl CRT missing)"; \
		aarch64-linux-gnu-gcc -nostdlib -static -fno-pie -no-pie -Os \
			-Wl,-Ttext-segment=0x43000000 -Wl,-e,_start \
			-o $@ $(KERNEL_ROOT)/setup/pid1/hello_aarch64_freestanding.c; \
	fi
	@file $@ | grep -qi 'aarch64'
	@file $@ | grep -qi 'static'
	@echo "✓ hello_aarch64 → $@ (ET_EXEC @ 0x43000000)"

# Keep the image blob separate from its C loader. The generic %.o: %.S rule in
# the root Makefile would otherwise lose the explicit hello dependency.
arch/arm64/sources/hello_embed.o: arch/arm64/sources/hello_embed.S $(MUSL_AARCH64_HELLO)
	@echo "  AS      $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Freestanding-ish flags for compile-only probes and the ARM64 common-core
# contract (no IR0_FREESTANDING_BOOT — retired alongside kernel-arm64-boot.bin).
ARM64_COMMON_CORE_COMPILE_CFLAGS = -ffreestanding -nostdlib -fno-builtin -O2 -mgeneral-regs-only \
	$(ARM64_BOARD_CFLAGS) \
	-I. -Iarch/arm64/include -Iarch/arm64/sources -Iarch/common -Iincludes -Iktm/include -Iincludes/ir0 \
	-ffunction-sections -fdata-sections \
	-DCONFIG_ENABLE_FS_MINIX=0 -DCONFIG_ENABLE_FS_SIMPLEFS=0 \
	-DCONFIG_ENABLE_FS_FAT16=0 -DCONFIG_ENABLE_FS_EXT2=0

# Board -min images (scripts/make/arm64-board.mk): tiny freestanding link @ firmware load address.
ARM64_BOOT_CFLAGS = -ffreestanding -nostdlib -fno-builtin -O2 -mgeneral-regs-only \
	-DIR0_FREESTANDING_BOOT=1 $(ARM64_BOARD_CFLAGS) \
	-I. -Iarch/arm64/include -Iarch/arm64/sources -Iarch/common -Iincludes -Iktm/include -Iincludes/ir0
ARM64_BOOT_ASFLAGS = -ffreestanding -nostdlib -mgeneral-regs-only

# QEMU virt: pin GICv2 (default may be v3 — product path uses the same GIC backend).
ARM64_QEMU_MACHINE = virt,gic-version=2

# F7b curated portable objs (not ALL_OBJS).
ARM64_SLICE_OBJS = \
	arch/arm64/sources/slice_hello.o \
	arch/arm64/sources/board.o \
	arch/arm64/sources/boot_info.o \
	arch/arm64/sources/fdt_reader.o \
	arch/arm64/sources/boot_info_platform.o \
	arch/arm64/sources/boot_info_devices.o \
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

.PHONY: arm64-slice-compile arm64-portable-compile arm64-all-objs-probe
arm64-slice-compile:
	@echo "  CC      ARM64_SLICE_OBJS (F7b)"
	@for f in $(ARM64_SLICE_OBJS); do \
		src=$${f%.o}.c; \
		echo "  CC      $$src"; \
		aarch64-linux-gnu-gcc $(ARM64_COMMON_CORE_COMPILE_CFLAGS) -c $$src -o $$f || exit 1; \
	done
	@echo "  CC      includes/string.c (compile-only probe, not linked — needs oops.h)"
	@aarch64-linux-gnu-gcc $(ARM64_COMMON_CORE_COMPILE_CFLAGS) \
		-c includes/string.c -o $(HOST_TMP)/ir0-string-arm64-probe.o
	@echo "✓ arm64-slice-compile OK (slice+pl011+serial_io + string.c probe)"

arm64-portable-compile: arm64-slice-compile
	@echo "  CC      ARM64_PORTABLE_OBJS (F7b P2 — not ALL_OBJS)"
	@for f in $(ARM64_PORTABLE_OBJS); do \
		src=$${f%.o}.c; \
		echo "  CC      $$src"; \
		aarch64-linux-gnu-gcc $(ARM64_COMMON_CORE_COMPILE_CFLAGS) -c $$src -o $$f || exit 1; \
	done
	@echo "  CC      arch/arm64/sources/irq_portable_stubs.c (INTERRUPT_OBJS_ARM64)"
	@aarch64-linux-gnu-gcc $(ARM64_COMMON_CORE_COMPILE_CFLAGS) -c \
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
		if ! aarch64-linux-gnu-gcc $(ARM64_COMMON_CORE_COMPILE_CFLAGS) -DARCH_ARM64=1 \
			-c $$src -o $(HOST_TMP)/ir0-arm64-probe-$$$$.o >> $(HOST_TMP)/ir0-arm64-all-objs-probe.log 2>&1; then \
			echo "FIRST_DIVERGENCE: $$src" | tee -a $(HOST_TMP)/ir0-arm64-all-objs-probe.log; \
			fail=1; \
			break; \
		fi; \
		rm -f $(HOST_TMP)/ir0-arm64-probe-$$$$.o; \
	done; \
	if [ $$fail -eq 0 ]; then \
		for src in kernel/main.c fs/open_flags.c \
			kernel/errno.c kernel/process/credentials.c \
			sched/switch/switch_arm64.c \
			kernel/time/clock_wait.c kernel/driver_registry.c \
			mm/copy_user.c \
			kernel/logging.c kernel/futex.c \
			net/net_compat.c; do \
			echo "  CC      $$src (KERNEL probe)"; \
			if ! aarch64-linux-gnu-gcc $(ARM64_COMMON_CORE_COMPILE_CFLAGS) -DARCH_ARM64=1 \
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
		echo "NEXT: make ARCH=arm64 kernel-arm64-product.bin (portable product link)"; \
		echo "✓ arm64-all-objs-probe: interrupt wall cleared; probe compile OK"; \
	else \
		echo "✗ arm64-all-objs-probe: see $(HOST_TMP)/ir0-arm64-all-objs-probe.log"; \
		tail -40 $(HOST_TMP)/ir0-arm64-all-objs-probe.log; \
		exit 1; \
	fi

# FDT-derived hypervisor banner (common boot_log) under QEMU virt — product kernel.
.PHONY: smoke-arm64-hypervisor-log
smoke-arm64-hypervisor-log: kernel-arm64-product.bin
	@if ! command -v qemu-system-aarch64 >/dev/null 2>&1; then \
		echo "⊘ smoke-arm64-hypervisor-log SKIP (qemu-system-aarch64 not in PATH)"; \
	else \
		echo "  SMOKE   ARM64 QEMU virt HYPERVISOR boot log (product)..."; \
		rm -f $(HOST_TMP)/arm64-hypervisor-smoke.log; \
		$(SMOKE_QEMU_RUN) --log $(HOST_TMP)/arm64-hypervisor-smoke.log \
			--timeout 25 --stale-sec 10 --mem 256M --done ARM64_BOOT_OK -- \
			qemu-system-aarch64 -M $(ARM64_QEMU_MACHINE) -cpu cortex-a57 \
			-kernel kernel-arm64-product.img -nographic -serial mon:stdio \
			-display none -no-reboot 2>/dev/null; \
		grep -q '\[HYPERVISOR\]' $(HOST_TMP)/arm64-hypervisor-smoke.log; \
		grep -Eq 'TCG \(QEMU TCG\)|vendor=KVM|Microsoft Hv|VMware|none detected' \
			$(HOST_TMP)/arm64-hypervisor-smoke.log || \
		grep -q 'Bare-metal execution confirmed' $(HOST_TMP)/arm64-hypervisor-smoke.log; \
		grep -q 'ARM64_BOOT_OK' $(HOST_TMP)/arm64-hypervisor-smoke.log; \
		echo "✓ smoke-arm64-hypervisor-log (HYPERVISOR line + ARM64_BOOT_OK)"; \
	fi

# Portable product kernel (ir0_kernel_boot, no IR0_FREESTANDING_BOOT): tag before /sbin/init handoff.
.PHONY: smoke-arm64-product-log
smoke-arm64-product-log: kernel-arm64-product.bin
	@if ! command -v qemu-system-aarch64 >/dev/null 2>&1; then \
		echo "⊘ smoke-arm64-product-log SKIP (qemu-system-aarch64 not in PATH)"; \
	else \
		echo "  SMOKE   ARM64 product kernel handoff tag..."; \
		rm -f $(HOST_TMP)/arm64-product-smoke.log; \
		$(SMOKE_QEMU_RUN) --log $(HOST_TMP)/arm64-product-smoke.log \
			--timeout 45 --stale-sec 15 --mem 256M --expected-panic \
			--done ARM64_PRODUCT_BOOT_HANDOFF -- \
			qemu-system-aarch64 -M $(ARM64_QEMU_MACHINE) -cpu cortex-a57 \
			-kernel kernel-arm64-product.img -nographic -serial mon:stdio \
			-display none -no-reboot 2>/dev/null && \
		grep -q 'ARM64_PRODUCT_BOOT_HANDOFF' $(HOST_TMP)/arm64-product-smoke.log && \
		echo "✓ smoke-arm64-product-log (ARM64_PRODUCT_BOOT_HANDOFF)"; \
	fi

.PHONY: smoke-arm64-bpivot-product
smoke-arm64-bpivot-product: kernel-arm64-product.bin $(MUSL_AARCH64_HELLO) scripts/make_test_cpio.py
	@if ! command -v qemu-system-aarch64 >/dev/null 2>&1; then \
		echo "⊘ smoke-arm64-bpivot-product SKIP (qemu-system-aarch64 not in PATH)"; \
	else \
		cpio=$$(mktemp "$${TMPDIR:-/tmp}/ir0-arm64-bpivot.XXXXXX"); \
		trap 'rm -f "$$cpio"' EXIT HUP INT TERM; \
		python3 scripts/make_test_cpio.py \
			--init $(MUSL_AARCH64_HELLO) --output "$$cpio"; \
		echo "  SMOKE   ARM64 portable bpivot boot (common fs/bpivot_boot.c)..."; \
		rm -f $(HOST_TMP)/arm64-bpivot-smoke.log; \
		$(SMOKE_QEMU_RUN) --log $(HOST_TMP)/arm64-bpivot-smoke.log \
			--timeout 60 --stale-sec 20 --mem 256M --expected-panic --success-mode all \
			--done ARM64_DTB_OK --done ARM64_DTB_USABLE_MEMORY_OK \
			--done ARM64_CPIO_ROOT_OK --done IR0_INITRAMFS_CPIO_OK \
			--done IR0_BPIVOT_COMMIT_OK --done ARM64_BPIVOT_COMMIT_OK \
			--done INIT_EXEC_LOADED_OK -- \
			qemu-system-aarch64 -M $(ARM64_QEMU_MACHINE) -cpu cortex-a57 \
			-global virtio-mmio.force-legacy=false \
			-kernel kernel-arm64-product.img -nographic -serial mon:stdio \
			-drive file="$$cpio",format=raw,if=none,id=bpivot \
			-device virtio-blk-device,drive=bpivot \
			-display none -no-reboot 2>/dev/null && \
		grep -q 'ARM64_DTB_OK' $(HOST_TMP)/arm64-bpivot-smoke.log && \
		grep -q 'ARM64_DTB_USABLE_MEMORY_OK' $(HOST_TMP)/arm64-bpivot-smoke.log && \
		grep -q 'ARM64_CPIO_ROOT_OK' $(HOST_TMP)/arm64-bpivot-smoke.log && \
		grep -q 'IR0_INITRAMFS_CPIO_OK' $(HOST_TMP)/arm64-bpivot-smoke.log && \
		grep -q 'IR0_BPIVOT_MOUNT_OK' $(HOST_TMP)/arm64-bpivot-smoke.log && \
		grep -q 'IR0_BPIVOT_COMMIT_OK' $(HOST_TMP)/arm64-bpivot-smoke.log && \
		grep -q 'ARM64_BPIVOT_COMMIT_OK' $(HOST_TMP)/arm64-bpivot-smoke.log && \
		grep -q 'INIT_EXEC_LOADED_OK' $(HOST_TMP)/arm64-bpivot-smoke.log && \
		test "$$(grep -c 'ARM64_SYNC_OTHER' \
			$(HOST_TMP)/arm64-bpivot-smoke.log || true)" -le 1 && \
		echo "✓ smoke-arm64-bpivot-product (CPIO fold + real root switch)"; \
	fi

# Handoff + first EL0 syscall from init (parity signal vs x86 product path): needs both tags.
.PHONY: smoke-arm64-product-usermode-log
smoke-arm64-product-usermode-log: kernel-arm64-product.bin
	@if ! command -v qemu-system-aarch64 >/dev/null 2>&1; then \
		echo "⊘ smoke-arm64-product-usermode-log SKIP (qemu-system-aarch64 not in PATH)"; \
	else \
		echo "  SMOKE   ARM64 product handoff + EL0 syscall..."; \
		rm -f $(HOST_TMP)/arm64-product-usermode-smoke.log; \
		$(SMOKE_QEMU_RUN) --log $(HOST_TMP)/arm64-product-usermode-smoke.log \
			--timeout 60 --stale-sec 20 --mem 256M --expected-panic --success-mode all \
			--done ARM64_PRODUCT_BOOT_HANDOFF --done ARM64_PRODUCT_EL0_SYSCALL_OK -- \
			qemu-system-aarch64 -M $(ARM64_QEMU_MACHINE) -cpu cortex-a57 \
			-kernel kernel-arm64-product.img -nographic -serial mon:stdio \
			-display none -no-reboot 2>/dev/null && \
		grep -q 'ARM64_PRODUCT_BOOT_HANDOFF' $(HOST_TMP)/arm64-product-usermode-smoke.log && \
		grep -q 'ARM64_PRODUCT_EL0_SYSCALL_OK' $(HOST_TMP)/arm64-product-usermode-smoke.log && \
		echo "✓ smoke-arm64-product-usermode-log (handoff + EL0 syscall)"; \
	fi


.PHONY: arm64-common-core-compile arm64-provider-audit

# Compile every architecture-neutral kernel/MM/library/VFS translation unit in
# an ARM64-only object namespace. This prevents a previous x86 build from
# satisfying an ARM prerequisite with an incompatible in-tree object. Drivers
# remain outside this set until the driver core selects them by platform.
ARM64_COMMON_CORE_INPUT_OBJS = \
	$(KERNEL_OBJS) $(MEMORY_OBJS) $(LIB_OBJS) $(FS_OBJS)
ARM64_COMMON_CORE_C_OBJS = $(filter %.o,$(filter-out \
	arch/% tests/% ktm/%,$(ARM64_COMMON_CORE_INPUT_OBJS)))
ARM64_COMMON_CORE_SRCS = $(ARM64_COMMON_CORE_C_OBJS:.o=.c)
ARM64_COMMON_CORE_BUILD_OBJS = $(addprefix build/arm64-common-core/,$(ARM64_COMMON_CORE_C_OBJS))

build/arm64-common-core/%.o: %.c
	@mkdir -p $(@D)
	@echo "  CC      $< (ARM64 common core)"
	@aarch64-linux-gnu-gcc $(ARM64_COMMON_CORE_COMPILE_CFLAGS) -DARCH_ARM64=1 \
		-I$(KERNEL_ROOT)/includes -I$(KERNEL_ROOT)/includes/ir0 \
		-I$(KERNEL_ROOT)/arch/common -I$(KERNEL_ROOT)/fs \
		-I$(KERNEL_ROOT)/net -I$(KERNEL_ROOT)/sched -I$(KERNEL_ROOT) \
		-c $< -o $@

arm64-common-core-compile: $(ARM64_COMMON_CORE_BUILD_OBJS)
	@test $(words $(ARM64_COMMON_CORE_SRCS)) -gt 80 || \
		{ echo "✗ ARM64 common-core inventory unexpectedly small"; exit 1; }
	@! printf '%s\n' $(ARM64_COMMON_CORE_SRCS) | grep -Eq '^arch/|^ktm/'
	@echo "✓ ARM64 common core compile contract ($$(printf '%s\n' $(ARM64_COMMON_CORE_SRCS) | wc -l) C units)"

# Policy: no extra staged semantic syscall TUs; no KTM harness allocator in the link.
arm64-provider-audit: kernel-arm64-product.bin
	@semantic=$$(find arch/arm64/sources -maxdepth 1 -type f \
		\( -name 'syscall_*_early.c' -o -name 'syscall_*_early.h' \) \
		! -name 'syscall_early.c' ! -name 'syscall_early.h' -print); \
	if [ -n "$$semantic" ]; then \
		echo "✗ semantic syscall provider leaked into ARM64:"; \
		printf '%s\n' "$$semantic"; exit 1; \
	fi
	@! aarch64-linux-gnu-nm kernel-arm64-product.bin | grep -q 'allocator_stubs' || \
		{ echo "✗ ARM64 product linked harness allocator_stubs"; exit 1; }
	@echo "✓ ARM64 provider audit (semantic syscall inventory + no harness allocator)"
