# SPDX-License-Identifier: GPL-3.0-only
#
# Userspace KTAP selftests. These complement KTM/ktest: they exercise the
# syscall surface from ring 3 and feed the Linux ABI audit when applicable.

IR0_SELFTEST_X86_BIN := $(KERNEL_ROOT)/build/selftests/x86_64/openat
IR0_SELFTEST_ARM64_BIN := $(KERNEL_ROOT)/build/selftests/arm64/openat
IR0_SELFTEST_X86_LOG := $(HOST_TMP)/ir0-selftest-x86-openat.log
IR0_SELFTEST_ARM64_LOG := $(HOST_TMP)/ir0-selftest-arm64-openat.log
IR0_SELFTEST_TAP_TEST := $(KERNEL_ROOT)/tests/host/test_selftest_tap.py

.PHONY: ir0-selftest-x86 ir0-selftest-arm64 ir0-selftest-openat

$(IR0_SELFTEST_X86_BIN): tests/selftests/syscall/openat.c tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 openat

$(IR0_SELFTEST_ARM64_BIN): tests/selftests/syscall/openat.c tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=arm64 openat

ir0-selftest-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_BIN)
	@python3 $(IR0_SELFTEST_TAP_TEST)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_BIN) \
		--log $(IR0_SELFTEST_X86_LOG)
	@echo "✓ ir0-selftest-x86 (openat KTAP)"

ir0-selftest-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_BIN)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_BIN) \
		--log $(IR0_SELFTEST_ARM64_LOG)
	@echo "✓ ir0-selftest-arm64 (openat KTAP)"

ir0-selftest-openat: ir0-selftest-x86 ir0-selftest-arm64
