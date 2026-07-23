# ARM64 multi-board (qemu-virt | rpi4 | rpi5) — freestanding early boot.
# Override: make ARM64_BOARD=rpi4 …  (daily virt smokes keep default qemu-virt)

ARM64_BOARD ?= qemu-virt

ifeq ($(ARM64_BOARD),rpi4)
ARM64_BOARD_CFLAGS := -DIR0_ARM64_BOARD_RPI4=1
else ifeq ($(ARM64_BOARD),rpi5)
ARM64_BOARD_CFLAGS := -DIR0_ARM64_BOARD_RPI5=1
else
ARM64_BOARD_CFLAGS := -DIR0_ARM64_BOARD_QEMU_VIRT=1
ARM64_BOARD := qemu-virt
endif

.PHONY: kernel-arm64-rpi4-min.bin kernel-arm64-rpi5-min.bin \
	smoke-arm64-rpi4-boot arm64-rpi4-compile arm64-rpi5-compile

# $1 = out dir, $2 = extra -D board flag
define ARM64_BOARD_MIN_BUILD
	@mkdir -p $(1)
	@for src in board_boot_min.c board.c pl011.c serial_io_arm64.c \
		platform.c freestanding_stubs.c; do \
		echo "  CC      arch/arm64/sources/$$src → $(1)/"; \
		aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) $(2) \
			-c arch/arm64/sources/$$src -o $(1)/$${src%.c}.o || exit 1; \
	done
	@aarch64-linux-gnu-gcc $(ARM64_BOOT_CFLAGS) $(2) -c \
		arch/common/boot_log.c -o $(1)/boot_log.o
	@echo "  LD      $@"
	@aarch64-linux-gnu-ld -T arch/arm64/linker_rpi.ld -o $@ \
		$(1)/board_boot_min.o $(1)/board.o $(1)/pl011.o \
		$(1)/serial_io_arm64.o $(1)/platform.o $(1)/freestanding_stubs.o \
		$(1)/boot_log.o
	@echo "✓ $@"
endef

kernel-arm64-rpi4-min.bin:
	@echo "  CC      ARM64 board=rpi4 min (load @ 0x80000)"
	$(call ARM64_BOARD_MIN_BUILD,build/arm64-rpi4,-UIR0_ARM64_BOARD_QEMU_VIRT -UIR0_ARM64_BOARD_RPI5 -DIR0_ARM64_BOARD_RPI4=1)

kernel-arm64-rpi5-min.bin:
	@echo "  CC      ARM64 board=rpi5 min stub (uart=none)"
	$(call ARM64_BOARD_MIN_BUILD,build/arm64-rpi5,-UIR0_ARM64_BOARD_QEMU_VIRT -UIR0_ARM64_BOARD_RPI4 -DIR0_ARM64_BOARD_RPI5=1)

arm64-rpi4-compile: kernel-arm64-rpi4-min.bin
	@if aarch64-linux-gnu-strings kernel-arm64-rpi4-min.bin | grep -q 'board=rpi4' && \
	   aarch64-linux-gnu-strings kernel-arm64-rpi4-min.bin | grep -q '0xfe201000'; then \
		echo "✓ arm64-rpi4-compile (board=rpi4 + uart_mmio=0xfe201000 in image)"; \
	else \
		echo "✗ arm64-rpi4-compile: missing rpi4 board strings"; exit 1; \
	fi

arm64-rpi5-compile: kernel-arm64-rpi5-min.bin
	@if aarch64-linux-gnu-strings kernel-arm64-rpi5-min.bin | grep -q 'board=rpi5' && \
	   aarch64-linux-gnu-strings kernel-arm64-rpi5-min.bin | grep -q 'ARM64_BOARD_RPI5_STUB'; then \
		echo "✓ arm64-rpi5-compile (board=rpi5 + ARM64_BOARD_RPI5_STUB in image)"; \
	else \
		echo "✗ arm64-rpi5-compile: missing rpi5 stub strings"; exit 1; \
	fi

smoke-arm64-rpi4-boot: kernel-arm64-rpi4-min.bin
	@if ! qemu-system-aarch64 -machine help 2>/dev/null | grep -q '^raspi4b[[:space:]]'; then \
		echo "⊘ smoke-arm64-rpi4-boot SKIP (QEMU has no raspi4b machine)"; \
	else \
		echo "  SMOKE   ARM64 QEMU raspi4b board min boot..."; \
		rm -f /tmp/arm64-rpi4-boot-smoke.log; \
		$(SMOKE_QEMU_RUN) --log /tmp/arm64-rpi4-boot-smoke.log --timeout 20 --stale-sec 8 \
			--done ARM64_BOOT_OK -- \
			qemu-system-aarch64 -M raspi4b \
			-kernel kernel-arm64-rpi4-min.bin -nographic -serial mon:stdio \
			-display none -no-reboot 2>/dev/null || true; \
		if grep -q 'IR0 Kernel v' /tmp/arm64-rpi4-boot-smoke.log && \
		   grep -q '\[BOOT\]' /tmp/arm64-rpi4-boot-smoke.log && \
		   grep -q 'board=rpi4' /tmp/arm64-rpi4-boot-smoke.log && \
		   grep -q 'ARM64_BOOT_OK' /tmp/arm64-rpi4-boot-smoke.log; then \
			echo "✓ smoke-arm64-rpi4-boot passed (banner + board=rpi4 + ARM64_BOOT_OK)"; \
			grep -E '\[BOOT\]|\[ARCH\]|ARM64_BOOT' /tmp/arm64-rpi4-boot-smoke.log | head -10; \
		else \
			echo "✗ smoke-arm64-rpi4-boot FAILED"; \
			head -40 /tmp/arm64-rpi4-boot-smoke.log; \
			exit 1; \
		fi; \
	fi
