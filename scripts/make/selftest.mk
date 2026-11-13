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
		--expect-log "USER_FAULT_FRAME kind=PAGE_FAULT hw=e " \
		--log $(HOST_TMP)/ir0-selftest-x86-fault.log
	@echo "✓ ir0-selftest-fault-x86"

ir0-selftest-fault-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_FAULT)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_FAULT) \
		--done "ok 3 segv_child_killed" \
		--expect-log "USER_FAULT_FRAME kind=PAGE_FAULT hw=24 " \
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

IR0_SELFTEST_X86_INIT_EXIT := $(KERNEL_ROOT)/build/selftests/x86_64/init_exit
IR0_SELFTEST_ARM64_INIT_EXIT := $(KERNEL_ROOT)/build/selftests/arm64/init_exit

.PHONY: ir0-selftest-init-exit-x86 ir0-selftest-init-exit-arm64 \
	ir0-selftest-init-exit

$(IR0_SELFTEST_X86_INIT_EXIT): tests/selftests/kernel/init_exit.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 init_exit

$(IR0_SELFTEST_ARM64_INIT_EXIT): tests/selftests/kernel/init_exit.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 init_exit

# The death of PID 1 panics with the wait status of its exit, as on Linux.
ir0-selftest-init-exit-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_INIT_EXIT)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 --expected-panic \
		--binary $(IR0_SELFTEST_X86_INIT_EXIT) \
		--done "Attempted to kill init! exitcode=0x00002a00" \
		--expect-log "ok 1 selftest_runs_as_init" \
		--log $(HOST_TMP)/ir0-selftest-x86-init-exit.log
	@echo "✓ ir0-selftest-init-exit-x86"

ir0-selftest-init-exit-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_INIT_EXIT)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 --expected-panic \
		--binary $(IR0_SELFTEST_ARM64_INIT_EXIT) \
		--done "Attempted to kill init! exitcode=0x00002a00" \
		--expect-log "ok 1 selftest_runs_as_init" \
		--log $(HOST_TMP)/ir0-selftest-arm64-init-exit.log
	@echo "✓ ir0-selftest-init-exit-arm64"

ir0-selftest-init-exit: ir0-selftest-init-exit-x86 ir0-selftest-init-exit-arm64

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
		--done "ok 5 segv_handler_returns_to_faulting_store" \
		--log $(HOST_TMP)/ir0-selftest-x86-fault-signals.log
	@echo "✓ ir0-selftest-fault-signals-x86"

ir0-selftest-fault-signals-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_FAULT_SIGNALS)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_FAULT_SIGNALS) \
		--done "ok 5 segv_handler_returns_to_faulting_store" \
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

IR0_SELFTEST_X86_UACCESS_KERNEL_ADDR := $(KERNEL_ROOT)/build/selftests/x86_64/uaccess_kernel_addr
IR0_SELFTEST_ARM64_UACCESS_KERNEL_ADDR := $(KERNEL_ROOT)/build/selftests/arm64/uaccess_kernel_addr

.PHONY: ir0-selftest-uaccess-x86 ir0-selftest-uaccess-arm64 ir0-selftest-uaccess

$(IR0_SELFTEST_X86_UACCESS_KERNEL_ADDR): tests/selftests/kernel/uaccess_kernel_addr.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 uaccess_kernel_addr

$(IR0_SELFTEST_ARM64_UACCESS_KERNEL_ADDR): tests/selftests/kernel/uaccess_kernel_addr.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 uaccess_kernel_addr

ir0-selftest-uaccess-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_UACCESS_KERNEL_ADDR)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_UACCESS_KERNEL_ADDR) \
		--done "ok 3 clock_gettime_to_kernel_addr_efault" \
		--log $(HOST_TMP)/ir0-selftest-x86-uaccess.log
	@echo "✓ ir0-selftest-uaccess-x86"

ir0-selftest-uaccess-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_UACCESS_KERNEL_ADDR)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_UACCESS_KERNEL_ADDR) \
		--done "ok 3 clock_gettime_to_kernel_addr_efault" \
		--log $(HOST_TMP)/ir0-selftest-arm64-uaccess.log
	@echo "✓ ir0-selftest-uaccess-arm64"

ir0-selftest-uaccess: ir0-selftest-uaccess-x86 ir0-selftest-uaccess-arm64

IR0_SELFTEST_X86_SIGNAL_RETURN := $(KERNEL_ROOT)/build/selftests/x86_64/signal_return
IR0_SELFTEST_ARM64_SIGNAL_RETURN := $(KERNEL_ROOT)/build/selftests/arm64/signal_return

.PHONY: ir0-selftest-signal-return-x86 ir0-selftest-signal-return-arm64 \
	ir0-selftest-signal-return

$(IR0_SELFTEST_X86_SIGNAL_RETURN): tests/selftests/kernel/signal_return.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 signal_return

$(IR0_SELFTEST_ARM64_SIGNAL_RETURN): tests/selftests/kernel/signal_return.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 signal_return

# A handler that returns resumes the interrupted code (rt_sigreturn).
ir0-selftest-signal-return-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_SIGNAL_RETURN)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_SIGNAL_RETURN) \
		--done "ok 2 handler_without_sa_restorer_follows_isa_abi" \
		--log $(HOST_TMP)/ir0-selftest-x86-signal-return.log
	@echo "✓ ir0-selftest-signal-return-x86"

ir0-selftest-signal-return-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_SIGNAL_RETURN)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_SIGNAL_RETURN) \
		--done "ok 2 handler_without_sa_restorer_follows_isa_abi" \
		--log $(HOST_TMP)/ir0-selftest-arm64-signal-return.log
	@echo "✓ ir0-selftest-signal-return-arm64"

ir0-selftest-signal-return: ir0-selftest-signal-return-x86 ir0-selftest-signal-return-arm64

IR0_SELFTEST_X86_SOCK_ZERO_LEN := $(KERNEL_ROOT)/build/selftests/x86_64/sock_zero_len
IR0_SELFTEST_ARM64_SOCK_ZERO_LEN := $(KERNEL_ROOT)/build/selftests/arm64/sock_zero_len

.PHONY: ir0-selftest-sock-zero-len-x86 ir0-selftest-sock-zero-len-arm64 ir0-selftest-sock-zero-len

$(IR0_SELFTEST_X86_SOCK_ZERO_LEN): tests/selftests/syscall/sock_zero_len.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 sock_zero_len

$(IR0_SELFTEST_ARM64_SOCK_ZERO_LEN): tests/selftests/syscall/sock_zero_len.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 sock_zero_len

# A zero-length send/recv is a no-op that succeeds, not -ENOMEM.
ir0-selftest-sock-zero-len-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_SOCK_ZERO_LEN)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_SOCK_ZERO_LEN) \
		--done "ok 11 dgram_recv_zero_len_consumes" \
		--log $(HOST_TMP)/ir0-selftest-x86-sock-zero-len.log
	@echo "✓ ir0-selftest-sock-zero-len-x86"

ir0-selftest-sock-zero-len-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_SOCK_ZERO_LEN)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_SOCK_ZERO_LEN) \
		--done "ok 11 dgram_recv_zero_len_consumes" \
		--log $(HOST_TMP)/ir0-selftest-arm64-sock-zero-len.log
	@echo "✓ ir0-selftest-sock-zero-len-arm64"

ir0-selftest-sock-zero-len: ir0-selftest-sock-zero-len-x86 ir0-selftest-sock-zero-len-arm64

IR0_SELFTEST_X86_PATH_TOO_LONG := $(KERNEL_ROOT)/build/selftests/x86_64/path_too_long
IR0_SELFTEST_ARM64_PATH_TOO_LONG := $(KERNEL_ROOT)/build/selftests/arm64/path_too_long

.PHONY: ir0-selftest-path-too-long-x86 ir0-selftest-path-too-long-arm64 ir0-selftest-path-too-long

$(IR0_SELFTEST_X86_PATH_TOO_LONG): tests/selftests/syscall/path_too_long.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 path_too_long

$(IR0_SELFTEST_ARM64_PATH_TOO_LONG): tests/selftests/syscall/path_too_long.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 path_too_long

# A path longer than PATH_MAX fails with ENAMETOOLONG, not EFAULT; mount(2)
# strings longer than PATH_MAX fail with EINVAL.
ir0-selftest-path-too-long-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_PATH_TOO_LONG)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_PATH_TOO_LONG) \
		--done "ok 6 mount_fstype_unknown_enodev" \
		--log $(HOST_TMP)/ir0-selftest-x86-path-too-long.log
	@echo "✓ ir0-selftest-path-too-long-x86"

ir0-selftest-path-too-long-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_PATH_TOO_LONG)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_PATH_TOO_LONG) \
		--done "ok 6 mount_fstype_unknown_enodev" \
		--log $(HOST_TMP)/ir0-selftest-arm64-path-too-long.log
	@echo "✓ ir0-selftest-path-too-long-arm64"

ir0-selftest-path-too-long: ir0-selftest-path-too-long-x86 ir0-selftest-path-too-long-arm64
IR0_SELFTEST_X86_UACCESS_READONLY := $(KERNEL_ROOT)/build/selftests/x86_64/uaccess_readonly
IR0_SELFTEST_ARM64_UACCESS_READONLY := $(KERNEL_ROOT)/build/selftests/arm64/uaccess_readonly

.PHONY: ir0-selftest-uaccess-readonly-x86 ir0-selftest-uaccess-readonly-arm64 \
	ir0-selftest-uaccess-readonly

$(IR0_SELFTEST_X86_UACCESS_READONLY): tests/selftests/kernel/uaccess_readonly.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 uaccess_readonly

$(IR0_SELFTEST_ARM64_UACCESS_READONLY): tests/selftests/kernel/uaccess_readonly.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 uaccess_readonly

# A syscall result is never written into a read-only user page.
ir0-selftest-uaccess-readonly-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_UACCESS_READONLY)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_UACCESS_READONLY) \
		--done "ok 3 fork_child_read_into_shared_rodata_efault" \
		--log $(HOST_TMP)/ir0-selftest-x86-uaccess-readonly.log
	@echo "✓ ir0-selftest-uaccess-readonly-x86"

ir0-selftest-uaccess-readonly-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_UACCESS_READONLY)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_UACCESS_READONLY) \
		--done "ok 3 fork_child_read_into_shared_rodata_efault" \
		--log $(HOST_TMP)/ir0-selftest-arm64-uaccess-readonly.log
	@echo "✓ ir0-selftest-uaccess-readonly-arm64"

ir0-selftest-uaccess-readonly: ir0-selftest-uaccess-readonly-x86 ir0-selftest-uaccess-readonly-arm64

IR0_SELFTEST_X86_UACCESS_KEEPS_DATA := $(KERNEL_ROOT)/build/selftests/x86_64/uaccess_efault_keeps_data
IR0_SELFTEST_ARM64_UACCESS_KEEPS_DATA := $(KERNEL_ROOT)/build/selftests/arm64/uaccess_efault_keeps_data

.PHONY: ir0-selftest-uaccess-keeps-data-x86 ir0-selftest-uaccess-keeps-data-arm64 \
	ir0-selftest-uaccess-keeps-data

$(IR0_SELFTEST_X86_UACCESS_KEEPS_DATA): tests/selftests/kernel/uaccess_efault_keeps_data.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 uaccess_efault_keeps_data

$(IR0_SELFTEST_ARM64_UACCESS_KEEPS_DATA): tests/selftests/kernel/uaccess_efault_keeps_data.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 uaccess_efault_keeps_data

# A read that fails with EFAULT consumes nothing (copy before dequeue).
ir0-selftest-uaccess-keeps-data-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_UACCESS_KEEPS_DATA)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_UACCESS_KEEPS_DATA) \
		--done "ok 3 pipe_read_efault_keeps_data" \
		--log $(HOST_TMP)/ir0-selftest-x86-uaccess-keeps-data.log
	@echo "✓ ir0-selftest-uaccess-keeps-data-x86"

ir0-selftest-uaccess-keeps-data-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_UACCESS_KEEPS_DATA)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_UACCESS_KEEPS_DATA) \
		--done "ok 3 pipe_read_efault_keeps_data" \
		--log $(HOST_TMP)/ir0-selftest-arm64-uaccess-keeps-data.log
	@echo "✓ ir0-selftest-uaccess-keeps-data-arm64"

ir0-selftest-uaccess-keeps-data: ir0-selftest-uaccess-keeps-data-x86 ir0-selftest-uaccess-keeps-data-arm64

IR0_SELFTEST_X86_BLOCKING_SLEEP := $(KERNEL_ROOT)/build/selftests/x86_64/blocking_sleep
IR0_SELFTEST_ARM64_BLOCKING_SLEEP := $(KERNEL_ROOT)/build/selftests/arm64/blocking_sleep

.PHONY: ir0-selftest-blocking-sleep-x86 ir0-selftest-blocking-sleep-arm64 \
	ir0-selftest-blocking-sleep

$(IR0_SELFTEST_X86_BLOCKING_SLEEP): tests/selftests/kernel/blocking_sleep.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 blocking_sleep

$(IR0_SELFTEST_ARM64_BLOCKING_SLEEP): tests/selftests/kernel/blocking_sleep.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 blocking_sleep

# Blocking syscalls sleep in the kernel and continue when woken (pipe read,
# wait4, nanosleep) or interrupted by a caught signal (EINTR).
ir0-selftest-blocking-sleep-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_BLOCKING_SLEEP)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_BLOCKING_SLEEP) \
		--done "ok 4 signal_interrupts_read" \
		--log $(HOST_TMP)/ir0-selftest-x86-blocking-sleep.log
	@echo "✓ ir0-selftest-blocking-sleep-x86"

ir0-selftest-blocking-sleep-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_BLOCKING_SLEEP)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_BLOCKING_SLEEP) \
		--done "ok 4 signal_interrupts_read" \
		--log $(HOST_TMP)/ir0-selftest-arm64-blocking-sleep.log
	@echo "✓ ir0-selftest-blocking-sleep-arm64"

ir0-selftest-blocking-sleep: ir0-selftest-blocking-sleep-x86 ir0-selftest-blocking-sleep-arm64

IR0_SELFTEST_X86_SLEEP_YIELDS := $(KERNEL_ROOT)/build/selftests/x86_64/sleep_yields
IR0_SELFTEST_ARM64_SLEEP_YIELDS := $(KERNEL_ROOT)/build/selftests/arm64/sleep_yields

.PHONY: ir0-selftest-sleep-yields-x86 ir0-selftest-sleep-yields-arm64 \
	ir0-selftest-sleep-yields

$(IR0_SELFTEST_X86_SLEEP_YIELDS): tests/selftests/kernel/sleep_yields.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 sleep_yields

$(IR0_SELFTEST_ARM64_SLEEP_YIELDS): tests/selftests/kernel/sleep_yields.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 sleep_yields

# A task sleeping in the kernel gives the CPU away: timed sleeps (nanosleep,
# a ppoll timeout) charge the sleeper no system time in /proc/self/stat.
ir0-selftest-sleep-yields-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_SLEEP_YIELDS)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_SLEEP_YIELDS) \
		--done "ok 2 ppoll_timeout_charges_no_cpu" \
		--log $(HOST_TMP)/ir0-selftest-x86-sleep-yields.log
	@echo "✓ ir0-selftest-sleep-yields-x86"

ir0-selftest-sleep-yields-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_SLEEP_YIELDS)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_SLEEP_YIELDS) \
		--done "ok 2 ppoll_timeout_charges_no_cpu" \
		--log $(HOST_TMP)/ir0-selftest-arm64-sleep-yields.log
	@echo "✓ ir0-selftest-sleep-yields-arm64"

ir0-selftest-sleep-yields: ir0-selftest-sleep-yields-x86 ir0-selftest-sleep-yields-arm64

IR0_SELFTEST_X86_FUTEX := $(KERNEL_ROOT)/build/selftests/x86_64/futex
IR0_SELFTEST_ARM64_FUTEX := $(KERNEL_ROOT)/build/selftests/arm64/futex

.PHONY: ir0-selftest-futex-x86 ir0-selftest-futex-arm64 \
	ir0-selftest-futex

$(IR0_SELFTEST_X86_FUTEX): tests/selftests/kernel/futex.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 futex

$(IR0_SELFTEST_ARM64_FUTEX): tests/selftests/kernel/futex.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 futex

# futex(2) wait/wake as on Linux, including a thread woken by its sibling and
# a wait ended by a caught signal (EINTR).
ir0-selftest-futex-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_FUTEX)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_FUTEX) \
		--done "ok 4 signal_interrupts_wait" \
		--log $(HOST_TMP)/ir0-selftest-x86-futex.log
	@echo "✓ ir0-selftest-futex-x86"

ir0-selftest-futex-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_FUTEX)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_FUTEX) \
		--done "ok 4 signal_interrupts_wait" \
		--log $(HOST_TMP)/ir0-selftest-arm64-futex.log
	@echo "✓ ir0-selftest-futex-arm64"

ir0-selftest-futex: ir0-selftest-futex-x86 ir0-selftest-futex-arm64

IR0_SELFTEST_X86_COW_MPROTECT := $(KERNEL_ROOT)/build/selftests/x86_64/cow_mprotect
IR0_SELFTEST_ARM64_COW_MPROTECT := $(KERNEL_ROOT)/build/selftests/arm64/cow_mprotect

.PHONY: ir0-selftest-cow-mprotect-x86 ir0-selftest-cow-mprotect-arm64 \
	ir0-selftest-cow-mprotect

$(IR0_SELFTEST_X86_COW_MPROTECT): tests/selftests/mm/cow_mprotect.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 cow_mprotect

$(IR0_SELFTEST_ARM64_COW_MPROTECT): tests/selftests/mm/cow_mprotect.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 cow_mprotect

# mprotect(PROT_READ) on a copy-on-write page after fork denies writes.
ir0-selftest-cow-mprotect-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_COW_MPROTECT)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_COW_MPROTECT) \
		--done "ok 3 cow_store_after_prot_write_private" \
		--log $(HOST_TMP)/ir0-selftest-x86-cow-mprotect.log
	@echo "✓ ir0-selftest-cow-mprotect-x86"

ir0-selftest-cow-mprotect-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_COW_MPROTECT)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_COW_MPROTECT) \
		--done "ok 3 cow_store_after_prot_write_private" \
		--log $(HOST_TMP)/ir0-selftest-arm64-cow-mprotect.log
	@echo "✓ ir0-selftest-cow-mprotect-arm64"

ir0-selftest-cow-mprotect: ir0-selftest-cow-mprotect-x86 ir0-selftest-cow-mprotect-arm64

IR0_SELFTEST_X86_SETGROUPS_EFAULT := $(KERNEL_ROOT)/build/selftests/x86_64/setgroups_efault
IR0_SELFTEST_ARM64_SETGROUPS_EFAULT := $(KERNEL_ROOT)/build/selftests/arm64/setgroups_efault

.PHONY: ir0-selftest-setgroups-efault-x86 ir0-selftest-setgroups-efault-arm64 \
	ir0-selftest-setgroups-efault

$(IR0_SELFTEST_X86_SETGROUPS_EFAULT): tests/selftests/syscall/setgroups_efault.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 setgroups_efault

$(IR0_SELFTEST_ARM64_SETGROUPS_EFAULT): tests/selftests/syscall/setgroups_efault.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 setgroups_efault

# A setgroups list that faults part-way changes no group.
ir0-selftest-setgroups-efault-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_SETGROUPS_EFAULT)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_SETGROUPS_EFAULT) \
		--done "ok 1 setgroups_crossing_unmapped_page_efault" \
		--log $(HOST_TMP)/ir0-selftest-x86-setgroups-efault.log
	@echo "✓ ir0-selftest-setgroups-efault-x86"

ir0-selftest-setgroups-efault-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_SETGROUPS_EFAULT)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_SETGROUPS_EFAULT) \
		--done "ok 1 setgroups_crossing_unmapped_page_efault" \
		--log $(HOST_TMP)/ir0-selftest-arm64-setgroups-efault.log
	@echo "✓ ir0-selftest-setgroups-efault-arm64"

ir0-selftest-setgroups-efault: ir0-selftest-setgroups-efault-x86 ir0-selftest-setgroups-efault-arm64

IR0_SELFTEST_X86_SIGNAL_RESTART := $(KERNEL_ROOT)/build/selftests/x86_64/signal_restart
IR0_SELFTEST_ARM64_SIGNAL_RESTART := $(KERNEL_ROOT)/build/selftests/arm64/signal_restart

.PHONY: ir0-selftest-signal-restart-x86 ir0-selftest-signal-restart-arm64 \
	ir0-selftest-signal-restart

$(IR0_SELFTEST_X86_SIGNAL_RESTART): tests/selftests/kernel/signal_restart.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 signal_restart

$(IR0_SELFTEST_ARM64_SIGNAL_RESTART): tests/selftests/kernel/signal_restart.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 signal_restart

# A caught signal ends a blocked syscall as on Linux: read and wait4 restart
# with SA_RESTART and fail with EINTR without it; nanosleep reports the
# time left.
ir0-selftest-signal-restart-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_SIGNAL_RESTART)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_SIGNAL_RESTART) \
		--done "ok 4 nanosleep_eintr_reports_remaining" \
		--log $(HOST_TMP)/ir0-selftest-x86-signal-restart.log
	@echo "✓ ir0-selftest-signal-restart-x86"

ir0-selftest-signal-restart-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_SIGNAL_RESTART)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_SIGNAL_RESTART) \
		--done "ok 4 nanosleep_eintr_reports_remaining" \
		--log $(HOST_TMP)/ir0-selftest-arm64-signal-restart.log
	@echo "✓ ir0-selftest-signal-restart-arm64"

ir0-selftest-signal-restart: ir0-selftest-signal-restart-x86 ir0-selftest-signal-restart-arm64

IR0_SELFTEST_X86_SIGRETURN_BAD_FRAME := $(KERNEL_ROOT)/build/selftests/x86_64/sigreturn_bad_frame
IR0_SELFTEST_ARM64_SIGRETURN_BAD_FRAME := $(KERNEL_ROOT)/build/selftests/arm64/sigreturn_bad_frame

.PHONY: ir0-selftest-sigreturn-bad-frame-x86 ir0-selftest-sigreturn-bad-frame-arm64 \
	ir0-selftest-sigreturn-bad-frame

$(IR0_SELFTEST_X86_SIGRETURN_BAD_FRAME): tests/selftests/kernel/sigreturn_bad_frame.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 sigreturn_bad_frame

$(IR0_SELFTEST_ARM64_SIGRETURN_BAD_FRAME): tests/selftests/kernel/sigreturn_bad_frame.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 sigreturn_bad_frame

# rt_sigreturn copies its frame from the user stack pointer only; a frame
# that cannot be copied in ends the task with SIGSEGV.
ir0-selftest-sigreturn-bad-frame-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_SIGRETURN_BAD_FRAME)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_SIGRETURN_BAD_FRAME) \
		--done "ok 2 kernel_frame_refused" \
		--log $(HOST_TMP)/ir0-selftest-x86-sigreturn-bad-frame.log
	@echo "✓ ir0-selftest-sigreturn-bad-frame-x86"

ir0-selftest-sigreturn-bad-frame-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_SIGRETURN_BAD_FRAME)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_SIGRETURN_BAD_FRAME) \
		--done "ok 2 kernel_frame_refused" \
		--log $(HOST_TMP)/ir0-selftest-arm64-sigreturn-bad-frame.log
	@echo "✓ ir0-selftest-sigreturn-bad-frame-arm64"

ir0-selftest-sigreturn-bad-frame: ir0-selftest-sigreturn-bad-frame-x86 ir0-selftest-sigreturn-bad-frame-arm64

IR0_SELFTEST_X86_FPSIMD := $(KERNEL_ROOT)/build/selftests/x86_64/fpsimd
IR0_SELFTEST_ARM64_FPSIMD := $(KERNEL_ROOT)/build/selftests/arm64/fpsimd

.PHONY: ir0-selftest-fpsimd-x86 ir0-selftest-fpsimd-arm64 ir0-selftest-fpsimd

$(IR0_SELFTEST_X86_FPSIMD): tests/selftests/kernel/fpsimd.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 fpsimd

$(IR0_SELFTEST_ARM64_FPSIMD): tests/selftests/kernel/fpsimd.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 fpsimd

# FP/SIMD registers are per-task: they survive context switches, a fork
# child inherits them and a signal handler cannot corrupt them.
ir0-selftest-fpsimd-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_FPSIMD)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_FPSIMD) \
		--done "ok 4 signal_handler_keeps_fpsimd" \
		--log $(HOST_TMP)/ir0-selftest-x86-fpsimd.log
	@echo "✓ ir0-selftest-fpsimd-x86"

ir0-selftest-fpsimd-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_FPSIMD)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_FPSIMD) \
		--done "ok 4 signal_handler_keeps_fpsimd" \
		--log $(HOST_TMP)/ir0-selftest-arm64-fpsimd.log
	@echo "✓ ir0-selftest-fpsimd-arm64"

ir0-selftest-fpsimd: ir0-selftest-fpsimd-x86 ir0-selftest-fpsimd-arm64

# Linux ABI groups whose red cases are all fixed (tests/selftests/kernel/
# linux_abi_<group>.c, one case per fixed divergence): sock (socket
# syscalls), mm (memory mapping and clocks) and fs (file and path
# syscalls). Built with the conformance
# runner, which fails the target unless every case listed in
# linux_abi_<group>.h passes.
IR0_ABI_SELFTESTS := sock mm fs
IR0_ABI_SELFTEST_RUN := python3 $(KERNEL_ROOT)/scripts/ir0_conformance.py run \
	--mode conformance --timeout 300

define ir0_abi_selftest
.PHONY: ir0-selftest-$(1)-abi-x86 ir0-selftest-$(1)-abi-arm64 ir0-selftest-$(1)-abi

ir0-selftest-$(1)-abi-x86: kernel-x64-userspace.iso
	@$$(MAKE) -C tests/conformance ARCH=x86_64 red RED=linux_abi_$(1)
	@$$(IR0_ABI_SELFTEST_RUN) --arch x86_64 \
		--cases $$(KERNEL_ROOT)/tests/selftests/kernel/linux_abi_$(1).h \
		--binary $$(KERNEL_ROOT)/build/selftests/x86_64/linux_abi_$(1) \
		--log $$(HOST_TMP)/ir0-selftest-x86-$(1)-abi.log
	@echo "✓ ir0-selftest-$(1)-abi-x86"

ir0-selftest-$(1)-abi-arm64: kernel-arm64-product.bin
	@$$(MAKE) -C tests/conformance ARCH=arm64 stub red RED=linux_abi_$(1)
	@$$(IR0_ABI_SELFTEST_RUN) --arch arm64 \
		--cases $$(KERNEL_ROOT)/tests/selftests/kernel/linux_abi_$(1).h \
		--binary $$(KERNEL_ROOT)/build/selftests/arm64/linux_abi_$(1) \
		--log $$(HOST_TMP)/ir0-selftest-arm64-$(1)-abi.log
	@echo "✓ ir0-selftest-$(1)-abi-arm64"

ir0-selftest-$(1)-abi: ir0-selftest-$(1)-abi-x86 ir0-selftest-$(1)-abi-arm64
endef

$(foreach g,$(IR0_ABI_SELFTESTS),$(eval $(call ir0_abi_selftest,$(g))))
