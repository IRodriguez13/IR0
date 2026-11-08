# SPDX-License-Identifier: GPL-3.0-only
#
# Userspace KTAP selftests. These complement KTM/ktest: they exercise the
# syscall surface from ring 3 and feed the Linux ABI audit when applicable.

IR0_SELFTEST_X86_OPENAT := $(KERNEL_ROOT)/build/selftests/x86_64/openat
IR0_SELFTEST_ARM64_OPENAT := $(KERNEL_ROOT)/build/selftests/arm64/openat
IR0_SELFTEST_X86_IDENTITY := $(KERNEL_ROOT)/build/selftests/x86_64/identity
IR0_SELFTEST_ARM64_IDENTITY := $(KERNEL_ROOT)/build/selftests/arm64/identity
IR0_SELFTEST_X86_FAULT := $(KERNEL_ROOT)/build/selftests/x86_64/fault
IR0_SELFTEST_ARM64_FAULT := $(KERNEL_ROOT)/build/selftests/arm64/fault
IR0_SELFTEST_TAP_TEST := $(KERNEL_ROOT)/tests/host/test_selftest_tap.py

.PHONY: ir0-selftest-x86 ir0-selftest-arm64 ir0-selftest-openat \
	ir0-selftest-identity-x86 ir0-selftest-identity-arm64 ir0-selftest-identity \
	ir0-selftest-fault-x86 ir0-selftest-fault-arm64 ir0-selftest-fault

$(IR0_SELFTEST_X86_OPENAT): tests/selftests/syscall/openat.c tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 openat

$(IR0_SELFTEST_ARM64_OPENAT): tests/selftests/syscall/openat.c tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=arm64 openat

$(IR0_SELFTEST_X86_IDENTITY): tests/selftests/syscall/identity.c tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 identity

$(IR0_SELFTEST_ARM64_IDENTITY): tests/selftests/syscall/identity.c tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=arm64 identity

ir0-selftest-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_OPENAT)
	@python3 $(IR0_SELFTEST_TAP_TEST)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_OPENAT) \
		--log $(HOST_TMP)/ir0-selftest-x86-openat.log
	@echo "✓ ir0-selftest-x86 (openat KTAP)"

ir0-selftest-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_OPENAT)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_OPENAT) \
		--log $(HOST_TMP)/ir0-selftest-arm64-openat.log
	@echo "✓ ir0-selftest-arm64 (openat KTAP)"

ir0-selftest-identity-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_IDENTITY)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_IDENTITY) \
		--log $(HOST_TMP)/ir0-selftest-x86-identity.log
	@echo "✓ ir0-selftest-identity-x86"

ir0-selftest-identity-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_IDENTITY)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_IDENTITY) \
		--log $(HOST_TMP)/ir0-selftest-arm64-identity.log
	@echo "✓ ir0-selftest-identity-arm64"

ir0-selftest-openat: ir0-selftest-x86 ir0-selftest-arm64
ir0-selftest-identity: ir0-selftest-identity-x86 ir0-selftest-identity-arm64

$(IR0_SELFTEST_X86_FAULT): tests/selftests/mm/fault.c tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 fault

$(IR0_SELFTEST_ARM64_FAULT): tests/selftests/mm/fault.c tests/selftests/ir0/ksft.h \
		tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 fault

ir0-selftest-fault-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_FAULT)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_FAULT) \
		--done "ok 3 segv_child_killed" \
		--log $(HOST_TMP)/ir0-selftest-x86-fault.log
	@echo "✓ ir0-selftest-fault-x86"

ir0-selftest-fault-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_FAULT)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_FAULT) \
		--done "ok 3 segv_child_killed" \
		--log $(HOST_TMP)/ir0-selftest-arm64-fault.log
	@echo "✓ ir0-selftest-fault-arm64"

ir0-selftest-fault: ir0-selftest-fault-x86 ir0-selftest-fault-arm64

IR0_SELFTEST_X86_PANIC_NESTED := $(KERNEL_ROOT)/build/selftests/x86_64/panic_nested
IR0_SELFTEST_ARM64_PANIC_NESTED := $(KERNEL_ROOT)/build/selftests/arm64/panic_nested

.PHONY: ir0-selftest-panic-nested-x86 ir0-selftest-panic-nested-arm64 \
	ir0-selftest-panic-nested

$(IR0_SELFTEST_X86_PANIC_NESTED): tests/selftests/kernel/panic_nested.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 panic_nested

$(IR0_SELFTEST_ARM64_PANIC_NESTED): tests/selftests/kernel/panic_nested.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 panic_nested

# A fault taken while panicking must end in panicex's nested branch.
ir0-selftest-panic-nested-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_PANIC_NESTED)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 --expected-panic \
		--binary $(IR0_SELFTEST_X86_PANIC_NESTED) \
		--done PANICEX_DOUBLE_FAULT_SAFE_OK \
		--log $(HOST_TMP)/ir0-selftest-x86-panic-nested.log
	@echo "✓ ir0-selftest-panic-nested-x86"

ir0-selftest-panic-nested-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_PANIC_NESTED)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 --expected-panic \
		--binary $(IR0_SELFTEST_ARM64_PANIC_NESTED) \
		--done PANICEX_DOUBLE_FAULT_SAFE_OK \
		--log $(HOST_TMP)/ir0-selftest-arm64-panic-nested.log
	@echo "✓ ir0-selftest-panic-nested-arm64"

ir0-selftest-panic-nested: ir0-selftest-panic-nested-x86 ir0-selftest-panic-nested-arm64

IR0_SELFTEST_X86_FAULT_SIGNALS := $(KERNEL_ROOT)/build/selftests/x86_64/fault_signals
IR0_SELFTEST_ARM64_FAULT_SIGNALS := $(KERNEL_ROOT)/build/selftests/arm64/fault_signals

.PHONY: ir0-selftest-fault-signals-x86 ir0-selftest-fault-signals-arm64 \
	ir0-selftest-fault-signals

$(IR0_SELFTEST_X86_FAULT_SIGNALS): tests/selftests/kernel/fault_signals.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 fault_signals

$(IR0_SELFTEST_ARM64_FAULT_SIGNALS): tests/selftests/kernel/fault_signals.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 fault_signals

ir0-selftest-fault-signals-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_FAULT_SIGNALS)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_FAULT_SIGNALS) \
		--done "ok 4 breakpoint_runs_sigtrap_handler" \
		--log $(HOST_TMP)/ir0-selftest-x86-fault-signals.log
	@echo "✓ ir0-selftest-fault-signals-x86"

ir0-selftest-fault-signals-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_FAULT_SIGNALS)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_FAULT_SIGNALS) \
		--done "ok 4 breakpoint_runs_sigtrap_handler" \
		--log $(HOST_TMP)/ir0-selftest-arm64-fault-signals.log
	@echo "✓ ir0-selftest-fault-signals-arm64"

ir0-selftest-fault-signals: ir0-selftest-fault-signals-x86 ir0-selftest-fault-signals-arm64

IR0_SELFTEST_X86_SIGNAL_SEMANTICS := $(KERNEL_ROOT)/build/selftests/x86_64/signal_semantics
IR0_SELFTEST_ARM64_SIGNAL_SEMANTICS := $(KERNEL_ROOT)/build/selftests/arm64/signal_semantics

.PHONY: ir0-selftest-signal-semantics-x86 ir0-selftest-signal-semantics-arm64 \
	ir0-selftest-signal-semantics

$(IR0_SELFTEST_X86_SIGNAL_SEMANTICS): tests/selftests/kernel/signal_semantics.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 signal_semantics

$(IR0_SELFTEST_ARM64_SIGNAL_SEMANTICS): tests/selftests/kernel/signal_semantics.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 signal_semantics

ir0-selftest-signal-semantics-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_SIGNAL_SEMANTICS)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_SIGNAL_SEMANTICS) \
		--done "ok 4 sigkill_reports_wifsignaled" \
		--log $(HOST_TMP)/ir0-selftest-x86-signal-semantics.log
	@echo "✓ ir0-selftest-signal-semantics-x86"

ir0-selftest-signal-semantics-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_SIGNAL_SEMANTICS)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_SIGNAL_SEMANTICS) \
		--done "ok 4 sigkill_reports_wifsignaled" \
		--log $(HOST_TMP)/ir0-selftest-arm64-signal-semantics.log
	@echo "✓ ir0-selftest-signal-semantics-arm64"

ir0-selftest-signal-semantics: ir0-selftest-signal-semantics-x86 ir0-selftest-signal-semantics-arm64
