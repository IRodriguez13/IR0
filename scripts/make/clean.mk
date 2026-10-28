# SPDX-License-Identifier: GPL-3.0-only
#
# `make clean` — remove generated build output, keep sources and runtime images.
#
# This fragment is loaded by the lean goal set, so `make clean` never parses the
# QA catalog (scripts/make/testing.mk). Keep the recipe free of product targets.

.PHONY: clean

# Trees that only contain generated output (gitignored).
IR0_CLEAN_DIRS := build iso iso_test includes/generated

# Root-level link/boot products (gitignored). Patterns stay narrow so tracked
# assets under dist/ or setup/ are never touched.
IR0_CLEAN_PRODUCTS := \
	kernel-x64.bin kernel-x64-test.bin \
	kernel-x64.iso kernel-x64-test.iso \
	kernel-x64-userspace.iso kernel-x64-userspace-lazy.iso \
	kernel-x64-userspace-ash-smoke.iso kernel-x64-ext2-root.iso \
	kernel-arm64.bin kernel-arm64-all.bin kernel-arm64-boot.bin \
	kernel-arm64-min.bin kernel-arm64-rpi3-min.bin \
	kernel-arm64-rpi4-min.bin kernel-arm64-rpi5-min.bin \
	kernel-arm64-mmu.bin kernel-arm64-vbar.bin kernel-arm64-el0.bin \
	kernel-arm64-slice.bin kernel-arm64-port.bin kernel-arm64-gic.bin \
	kernel-arm64-syscall.bin kernel-arm64-rpi3-early.bin \
	kernel-arm64-Image kernel-arm64-all-Image \
	kernel-x64.map kernel-arm64.map kernel-x64.disasm \
	compile_commands.json qemu_debug.log

clean:
	@echo "  CLEAN   objects, deps, images, generated headers"
	@rm -rf $(IR0_CLEAN_DIRS)
	@rm -f $(IR0_CLEAN_PRODUCTS)
	@find . \( -path ./.git -o -path ./dist -o -path ./out \
		-o -path ./toolchain -o -path ./third-party \
		-o -path ./setup/third-party -o -path ./.cache \
		-o -path ./rust/target \) -prune -o \
		-type f \( -name '*.o' -o -name '*.d' -o -name '*.su' \) \
		-exec rm -f {} +
	@$(MAKE) -s --no-print-directory -C tests/host clean
	@$(MAKE) -s --no-print-directory -C tests/kernel_memsafe clean
	@rm -f tests/ktm/userdev/ktm_*_case
	@echo "✓ clean done (disk images, dist/ and out/ preserved)"
