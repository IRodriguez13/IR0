# SPDX-License-Identifier: GPL-3.0-only
#
# `make clean` — remove generated build output, keep sources, disks and caches.

.PHONY: clean

# Trees that only contain generated output (gitignored).
IR0_CLEAN_DIRS := build iso iso_test includes/generated

# Root-level link/boot products (gitignored). Patterns stay narrow so tracked
# assets under dist/, setup/ or Documentation/ are never touched.
IR0_CLEAN_PRODUCTS := \
	kernel-x64.bin kernel-x64-test.bin \
	kernel-x64.iso kernel-x64-test.iso \
	kernel-arm64.bin kernel-arm64-all.bin kernel-arm64-boot.bin \
	kernel-arm64-min.bin kernel-arm64-rpi3-min.bin \
	kernel-arm64-rpi4-min.bin kernel-arm64-rpi5-min.bin \
	kernel-arm64-mmu.bin kernel-arm64-vbar.bin kernel-arm64-el0.bin \
	kernel-arm64-slice.bin kernel-arm64-port.bin kernel-arm64-gic.bin \
	kernel-arm64-syscall.bin \
	kernel-arm64-Image kernel-arm64-all-Image \
	kernel-x64.map kernel-arm64.map kernel-x64.disasm \
	compile_commands.json qemu_debug.log

IR0_CLEAN_TEST_BINS := \
	tests/host/ir0_test_suite tests/kernel_memsafe/ir0_kernel_memsafe

clean:
	@echo "  CLEAN   objects, deps, images, generated headers"
	@rm -rf $(IR0_CLEAN_DIRS)
	@rm -f $(IR0_CLEAN_PRODUCTS) $(IR0_CLEAN_TEST_BINS)
	@find . \( -path ./.git -o -path ./dist -o -path ./out \
		-o -path ./toolchain -o -path ./third-party \
		-o -path ./setup/third-party -o -path ./.cache \
		-o -path ./rust/target \) -prune -o \
		-type f \( -name '*.o' -o -name '*.d' -o -name '*.su' \) \
		-exec rm -f {} +
	@echo "✓ clean done (disk images, toolchain/, staging caches, dist/ and out/ preserved)"
