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

# Smoke binaries and helpers the QA catalog links inside the source tree.
# Keep this list in sync with the compiled-artifact entries in .gitignore.
IR0_CLEAN_SMOKE_BINS := \
	setup/doom/doomgeneric_smoke \
	setup/pid1/f41true setup/pid1/busybox_real \
	setup/pid1/ktm_doom_interactive \
	setup/pid1/busybox_manifest_smoke setup/pid1/ktm_boot_halt \
	setup/pid1/ktm_fbdev_gui setup/pid1/init \
	setup/pid1/musl_arch_prctl_smoke setup/pid1/musl_pthread_smoke \
	setup/pid1/musl_pthread_libc_smoke setup/pid1/setuid_exec_smoke \
	setup/pid1/setid_helper setup/pid1/sh_smoke \
	setup/pid1/userspace_segv setup/pid1/sbin/irinit \
	setup/pid1/tcc_power_halt_harness setup/pid1/init_hostshare_exec \
	setup/pid1/pipeline_stress_smoke setup/pid1/session_walk_smoke \
	setup/pid1/cmd_stress_smoke setup/pid1/tty_raw_probe \
	setup/pid1/fase50_busybox_real setup/pid1/fase52_harness \
	setup/libkconfig_build.a scripts/kconfig/libkconfig_build.a \
	tests/host/ir0_test_suite tests/kernel_memsafe/ir0_kernel_memsafe

clean:
	@echo "  CLEAN   objects, deps, images, generated headers"
	@rm -rf $(IR0_CLEAN_DIRS)
	@rm -f $(IR0_CLEAN_PRODUCTS) $(IR0_CLEAN_SMOKE_BINS)
	@find . \( -path ./.git -o -path ./dist -o -path ./out \
		-o -path ./toolchain -o -path ./third-party \
		-o -path ./setup/third-party -o -path ./.cache \
		-o -path ./rust/target \) -prune -o \
		-type f \( -name '*.o' -o -name '*.d' -o -name '*.su' \) \
		-exec rm -f {} +
	@rm -f tests/ktm/userdev/ktm_*_case
	@echo "✓ clean done (disk images, toolchain/, staging caches, dist/ and out/ preserved)"
