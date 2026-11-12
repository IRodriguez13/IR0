# SPDX-License-Identifier: GPL-3.0-only
#
# Linux ABI conformance harness, bounded syscall fuzzer and the red selftests
# they produced. See Documentation/conformance.md.
#
#   make ir0-conformance-x86 | ir0-conformance-arm64      [LTP_SRC=<ltp tree>]
#   make ir0-fuzz-x86 | ir0-fuzz-arm64 SEED=<n> COUNT=<n> [BATCH=<b> TRACE=1]
#   make ir0-selftest-red-signals-{x86,arm64}
#
# Every guest boots through scripts/ir0_conformance.py -> smoke_qemu_run.sh
# (256 MiB guest, hard timeout, stale-output window, process-group kill).
# None of these targets is part of test-fast or ctr: the red selftests fail
# on purpose until their kernel bugs are fixed.

CONFORMANCE_RUN := python3 $(KERNEL_ROOT)/scripts/ir0_conformance.py
CONFORMANCE_DIR := $(KERNEL_ROOT)/tests/conformance
CONFORMANCE_SRCS := $(wildcard $(CONFORMANCE_DIR)/*.c $(CONFORMANCE_DIR)/*.h) \
	$(CONFORMANCE_DIR)/Makefile
SEED ?= 1
COUNT ?= 2000
BATCH ?= 0
TRACE ?= 0
LTP_SRC ?=
LTP_CC_X86 ?= x86_64-linux-musl-gcc

.PHONY: ir0-conformance-x86 ir0-conformance-arm64 ir0-conformance \
	ir0-fuzz-x86 ir0-fuzz-arm64 ir0-conformance-host-check

ir0-conformance-host-check:
	@python3 $(KERNEL_ROOT)/tests/host/test_conformance_judge.py

$(KERNEL_ROOT)/build/conformance/x86_64/conformance: $(CONFORMANCE_SRCS)
	@$(MAKE) -C tests/conformance ARCH=x86_64 conformance

$(KERNEL_ROOT)/build/conformance/arm64/conformance: $(CONFORMANCE_SRCS)
	@$(MAKE) -C tests/conformance ARCH=arm64 conformance stub

# LTP is optional and never downloaded: LTP_SRC names an existing checkout.
ir0-conformance-x86: kernel-x64-userspace.iso \
		$(KERNEL_ROOT)/build/conformance/x86_64/conformance ir0-conformance-host-check
	@ltp=""; \
	if [ -n "$(LTP_SRC)" ]; then \
		ltp="$(HOST_TMP)/ir0-ltp-x86"; \
		$(CONFORMANCE_RUN) ltp-build --ltp-src "$(LTP_SRC)" \
			--cases $(CONFORMANCE_DIR)/ltp-cases.txt \
			--out "$$ltp" --cc $(LTP_CC_X86) || exit 1; \
	fi; \
	$(CONFORMANCE_RUN) run --arch x86_64 --mode conformance \
		--binary $(KERNEL_ROOT)/build/conformance/x86_64/conformance \
		--baseline $(CONFORMANCE_DIR)/baseline-x86_64.txt \
		--log $(HOST_TMP)/ir0-conformance-x86.log \
		--summary $(HOST_TMP)/ir0-conformance-x86.json \
		$${ltp:+--ltp-dir "$$ltp"}
	@echo "✓ ir0-conformance-x86 (no regressions against the baseline)"

ir0-conformance-arm64: kernel-arm64-product.bin \
		$(KERNEL_ROOT)/build/conformance/arm64/conformance ir0-conformance-host-check
	@$(CONFORMANCE_RUN) run --arch arm64 --mode conformance \
		--binary $(KERNEL_ROOT)/build/conformance/arm64/conformance \
		--baseline $(CONFORMANCE_DIR)/baseline-arm64.txt \
		--log $(HOST_TMP)/ir0-conformance-arm64.log \
		--summary $(HOST_TMP)/ir0-conformance-arm64.json
	@echo "✓ ir0-conformance-arm64 (no regressions against the baseline)"

ir0-conformance: ir0-conformance-x86 ir0-conformance-arm64

# The fuzz binary bakes SEED/COUNT/BATCH/TRACE in, so a rerun with the same
# values replays the same syscall sequence.
ir0-fuzz-x86: kernel-x64-userspace.iso
	@$(MAKE) -C tests/conformance ARCH=x86_64 fuzz \
		SEED=$(SEED) COUNT=$(COUNT) BATCH=$(BATCH) TRACE=$(TRACE)
	@$(CONFORMANCE_RUN) run --arch x86_64 --mode fuzz \
		--binary $(KERNEL_ROOT)/build/conformance/x86_64/fuzz-s$(SEED)-n$(COUNT)-b$(BATCH)-t$(TRACE) \
		--timeout 600 \
		--log $(HOST_TMP)/ir0-fuzz-x86-s$(SEED).log \
		--summary $(HOST_TMP)/ir0-fuzz-x86-s$(SEED).json
	@echo "✓ ir0-fuzz-x86 seed=$(SEED) count=$(COUNT)"

ir0-fuzz-arm64: kernel-arm64-product.bin
	@$(MAKE) -C tests/conformance ARCH=arm64 stub fuzz \
		SEED=$(SEED) COUNT=$(COUNT) BATCH=$(BATCH) TRACE=$(TRACE)
	@$(CONFORMANCE_RUN) run --arch arm64 --mode fuzz \
		--binary $(KERNEL_ROOT)/build/conformance/arm64/fuzz-s$(SEED)-n$(COUNT)-b$(BATCH)-t$(TRACE) \
		--timeout 600 \
		--log $(HOST_TMP)/ir0-fuzz-arm64-s$(SEED).log \
		--summary $(HOST_TMP)/ir0-fuzz-arm64-s$(SEED).json
	@echo "✓ ir0-fuzz-arm64 seed=$(SEED) count=$(COUNT)"

# Red selftests: one target per area and ISA.
IR0_RED_SELFTESTS := signals

define ir0_red_selftest
.PHONY: ir0-selftest-red-$(1)-x86 ir0-selftest-red-$(1)-arm64
ir0-selftest-red-$(1)-x86: kernel-x64-userspace.iso
	@$$(MAKE) -C tests/conformance ARCH=x86_64 red RED=linux_abi_$(1)
	@$$(CONFORMANCE_RUN) run --arch x86_64 --mode conformance --timeout 300 \
		--binary $$(KERNEL_ROOT)/build/selftests/x86_64/linux_abi_$(1) \
		--cases $$(KERNEL_ROOT)/tests/selftests/kernel/linux_abi_$(1).h \
		--log $$(HOST_TMP)/ir0-selftest-red-$(1)-x86.log
	@echo "✓ ir0-selftest-red-$(1)-x86"

ir0-selftest-red-$(1)-arm64: kernel-arm64-product.bin
	@$$(MAKE) -C tests/conformance ARCH=arm64 stub red RED=linux_abi_$(1)
	@$$(CONFORMANCE_RUN) run --arch arm64 --mode conformance --timeout 300 \
		--binary $$(KERNEL_ROOT)/build/selftests/arm64/linux_abi_$(1) \
		--cases $$(KERNEL_ROOT)/tests/selftests/kernel/linux_abi_$(1).h \
		--log $$(HOST_TMP)/ir0-selftest-red-$(1)-arm64.log
	@echo "✓ ir0-selftest-red-$(1)-arm64"
endef

$(foreach t,$(IR0_RED_SELFTESTS),$(eval $(call ir0_red_selftest,$(t))))
