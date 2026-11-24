# SPDX-License-Identifier: GPL-3.0-only
#
# Userspace KTAP selftests. These complement KTM/ktest: they exercise the
# syscall surface from ring 3 and feed the Linux ABI audit when applicable.

IR0_SELFTEST_X86_OPENAT := $(KERNEL_ROOT)/build/selftests/x86_64/openat
IR0_SELFTEST_ARM64_OPENAT := $(KERNEL_ROOT)/build/selftests/arm64/openat
IR0_SELFTEST_X86_IDENTITY := $(KERNEL_ROOT)/build/selftests/x86_64/identity
IR0_SELFTEST_ARM64_IDENTITY := $(KERNEL_ROOT)/build/selftests/arm64/identity
IR0_SELFTEST_X86_NOFILE := $(KERNEL_ROOT)/build/selftests/x86_64/nofile
IR0_SELFTEST_ARM64_NOFILE := $(KERNEL_ROOT)/build/selftests/arm64/nofile
IR0_SELFTEST_X86_VMCOMMIT := $(KERNEL_ROOT)/build/selftests/x86_64/vm_commit
IR0_SELFTEST_ARM64_VMCOMMIT := $(KERNEL_ROOT)/build/selftests/arm64/vm_commit
IR0_SELFTEST_X86_UCONTEXT := $(KERNEL_ROOT)/build/selftests/x86_64/ucontext
IR0_SELFTEST_ARM64_UCONTEXT := $(KERNEL_ROOT)/build/selftests/arm64/ucontext
IR0_SELFTEST_X86_FAULT := $(KERNEL_ROOT)/build/selftests/x86_64/fault
IR0_SELFTEST_ARM64_FAULT := $(KERNEL_ROOT)/build/selftests/arm64/fault
IR0_SELFTEST_TAP_TEST := $(KERNEL_ROOT)/tests/host/test_selftest_tap.py

.PHONY: ir0-selftest-x86 ir0-selftest-arm64 ir0-selftest-openat \
	ir0-selftest-identity-x86 ir0-selftest-identity-arm64 ir0-selftest-identity \
	ir0-selftest-nofile-x86 ir0-selftest-nofile-arm64 ir0-selftest-nofile \
	ir0-selftest-vm-commit-x86 ir0-selftest-vm-commit-arm64 ir0-selftest-vm-commit \
	ir0-selftest-ucontext-x86 ir0-selftest-ucontext-arm64 ir0-selftest-ucontext \
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

$(IR0_SELFTEST_X86_NOFILE): tests/selftests/syscall/nofile.c tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 nofile

$(IR0_SELFTEST_ARM64_NOFILE): tests/selftests/syscall/nofile.c tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=arm64 nofile

ir0-selftest-nofile-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_NOFILE)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_NOFILE) \
		--done "ok 3 emfile_at_soft" \
		--log $(HOST_TMP)/ir0-selftest-x86-nofile.log
	@echo "✓ ir0-selftest-nofile-x86"

ir0-selftest-nofile-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_NOFILE)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_NOFILE) \
		--done "ok 3 emfile_at_soft" \
		--log $(HOST_TMP)/ir0-selftest-arm64-nofile.log
	@echo "✓ ir0-selftest-nofile-arm64"

ir0-selftest-nofile: ir0-selftest-nofile-x86 ir0-selftest-nofile-arm64

$(IR0_SELFTEST_X86_VMCOMMIT): tests/selftests/syscall/vm_commit.c tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 vm_commit

$(IR0_SELFTEST_ARM64_VMCOMMIT): tests/selftests/syscall/vm_commit.c tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=arm64 vm_commit

ir0-selftest-vm-commit-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_VMCOMMIT)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_VMCOMMIT) \
		--done "ok 3 mprotect_lazy" \
		--log $(HOST_TMP)/ir0-selftest-x86-vm-commit.log
	@echo "✓ ir0-selftest-vm-commit-x86"

ir0-selftest-vm-commit-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_VMCOMMIT)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_VMCOMMIT) \
		--done "ok 3 mprotect_lazy" \
		--log $(HOST_TMP)/ir0-selftest-arm64-vm-commit.log
	@echo "✓ ir0-selftest-vm-commit-arm64"

ir0-selftest-vm-commit: ir0-selftest-vm-commit-x86 ir0-selftest-vm-commit-arm64

$(IR0_SELFTEST_X86_UCONTEXT): tests/selftests/syscall/ucontext.c tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 ucontext

$(IR0_SELFTEST_ARM64_UCONTEXT): tests/selftests/syscall/ucontext.c tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=arm64 ucontext

ir0-selftest-ucontext-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_UCONTEXT)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_UCONTEXT) \
		--done "ok 1 ucontext_sets_ip" \
		--log $(HOST_TMP)/ir0-selftest-x86-ucontext.log
	@echo "✓ ir0-selftest-ucontext-x86"

ir0-selftest-ucontext-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_UCONTEXT)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_UCONTEXT) \
		--done "ok 1 ucontext_sets_ip" \
		--log $(HOST_TMP)/ir0-selftest-arm64-ucontext.log
	@echo "✓ ir0-selftest-ucontext-arm64"

ir0-selftest-ucontext: ir0-selftest-ucontext-x86 ir0-selftest-ucontext-arm64

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
# syscalls), mm (memory mapping and clocks), fs (file and path syscalls),
# signals (delivery, masks and default actions), uaccess (user-pointer
# checks that ran ahead of the copy), kexec (kexec_load architecture
# check), tty (console line discipline, TIOCSTI-driven), sigframe
# (handler frame placement and the mask after longjmp), sleep
# (clock_nanosleep and sched_yield) and input (evdev and uinput).
# Built with the conformance runner, which fails the target unless every
# case listed in linux_abi_<group>.h passes.
IR0_ABI_SELFTESTS := sock mm fs signals uaccess kexec tty sigframe sleep input
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
	@$$(MAKE) -C tests/conformance ARCH=arm64 red RED=linux_abi_$(1)
	@$$(IR0_ABI_SELFTEST_RUN) --arch arm64 \
		--cases $$(KERNEL_ROOT)/tests/selftests/kernel/linux_abi_$(1).h \
		--binary $$(KERNEL_ROOT)/build/selftests/arm64/linux_abi_$(1) \
		--log $$(HOST_TMP)/ir0-selftest-arm64-$(1)-abi.log
	@echo "✓ ir0-selftest-$(1)-abi-arm64"

ir0-selftest-$(1)-abi: ir0-selftest-$(1)-abi-x86 ir0-selftest-$(1)-abi-arm64
endef

$(foreach g,$(IR0_ABI_SELFTESTS),$(eval $(call ir0_abi_selftest,$(g))))

# SIGHAND-SELFTEST
IR0_SELFTEST_X86_SIGHAND := $(KERNEL_ROOT)/build/selftests/x86_64/sighand
IR0_SELFTEST_ARM64_SIGHAND := $(KERNEL_ROOT)/build/selftests/arm64/sighand

.PHONY: ir0-selftest-sighand-x86 ir0-selftest-sighand-arm64 ir0-selftest-sighand

$(IR0_SELFTEST_X86_SIGHAND): tests/selftests/syscall/sighand.c tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 sighand

$(IR0_SELFTEST_ARM64_SIGHAND): tests/selftests/syscall/sighand.c tests/selftests/ir0/ksft.h \
		tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 sighand

ir0-selftest-sighand-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_SIGHAND)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_SIGHAND) \
		--done "ok 2 sighand_shared" \
		--log $(HOST_TMP)/ir0-selftest-x86-sighand.log
	@echo "✓ ir0-selftest-sighand-x86"

ir0-selftest-sighand-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_SIGHAND)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_SIGHAND) \
		--done "ok 2 sighand_shared" \
		--log $(HOST_TMP)/ir0-selftest-arm64-sighand.log
	@echo "✓ ir0-selftest-sighand-arm64"

ir0-selftest-sighand: ir0-selftest-sighand-x86 ir0-selftest-sighand-arm64

# ACCEPT4-SELFTEST
IR0_SELFTEST_X86_ACCEPT4 := $(KERNEL_ROOT)/build/selftests/x86_64/accept4
IR0_SELFTEST_ARM64_ACCEPT4 := $(KERNEL_ROOT)/build/selftests/arm64/accept4

.PHONY: ir0-selftest-accept4-x86 ir0-selftest-accept4-arm64 ir0-selftest-accept4

$(IR0_SELFTEST_X86_ACCEPT4): tests/selftests/syscall/accept4.c tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 accept4

$(IR0_SELFTEST_ARM64_ACCEPT4): tests/selftests/syscall/accept4.c tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=arm64 accept4

ir0-selftest-accept4-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_ACCEPT4)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_ACCEPT4) \
		--done "ok 1 accept4_peer_addr" \
		--log $(HOST_TMP)/ir0-selftest-x86-accept4.log
	@echo "✓ ir0-selftest-accept4-x86"

ir0-selftest-accept4-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_ACCEPT4)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_ACCEPT4) \
		--done "ok 1 accept4_peer_addr" \
		--log $(HOST_TMP)/ir0-selftest-arm64-accept4.log
	@echo "✓ ir0-selftest-accept4-arm64"

ir0-selftest-accept4: ir0-selftest-accept4-x86 ir0-selftest-accept4-arm64

# RT-SIG-SELFTEST
IR0_SELFTEST_X86_RT_SIG := $(KERNEL_ROOT)/build/selftests/x86_64/rt_sig
IR0_SELFTEST_ARM64_RT_SIG := $(KERNEL_ROOT)/build/selftests/arm64/rt_sig

.PHONY: ir0-selftest-rt-sig-x86 ir0-selftest-rt-sig-arm64 ir0-selftest-rt-sig

$(IR0_SELFTEST_X86_RT_SIG): tests/selftests/syscall/rt_sig.c tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 rt_sig

$(IR0_SELFTEST_ARM64_RT_SIG): tests/selftests/syscall/rt_sig.c tests/selftests/ir0/ksft.h \
		tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 rt_sig

# Signal 34 is a real-time signal: rt_sigaction installs a handler and both
# kill and tgkill run it.
ir0-selftest-rt-sig-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_RT_SIG)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_RT_SIG) \
		--done "ok 1 rt_sig_34_delivered" \
		--log $(HOST_TMP)/ir0-selftest-x86-rt-sig.log
	@echo "✓ ir0-selftest-rt-sig-x86"

ir0-selftest-rt-sig-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_RT_SIG)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_RT_SIG) \
		--done "ok 1 rt_sig_34_delivered" \
		--log $(HOST_TMP)/ir0-selftest-arm64-rt-sig.log
	@echo "✓ ir0-selftest-rt-sig-arm64"

ir0-selftest-rt-sig: ir0-selftest-rt-sig-x86 ir0-selftest-rt-sig-arm64

# SYSFS-CPU-SELFTEST
IR0_SELFTEST_X86_SYSFS_CPU := $(KERNEL_ROOT)/build/selftests/x86_64/sysfs_cpu
IR0_SELFTEST_ARM64_SYSFS_CPU := $(KERNEL_ROOT)/build/selftests/arm64/sysfs_cpu

.PHONY: ir0-selftest-sysfs-cpu-x86 ir0-selftest-sysfs-cpu-arm64 ir0-selftest-sysfs-cpu

$(IR0_SELFTEST_X86_SYSFS_CPU): tests/selftests/syscall/sysfs_cpu.c tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 sysfs_cpu

$(IR0_SELFTEST_ARM64_SYSFS_CPU): tests/selftests/syscall/sysfs_cpu.c tests/selftests/ir0/ksft.h \
		tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 sysfs_cpu

ir0-selftest-sysfs-cpu-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_SYSFS_CPU)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_SYSFS_CPU) \
		--done "ok 1 cpu0_online" \
		--log $(HOST_TMP)/ir0-selftest-x86-sysfs-cpu.log
	@echo "✓ ir0-selftest-sysfs-cpu-x86"

ir0-selftest-sysfs-cpu-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_SYSFS_CPU)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_SYSFS_CPU) \
		--done "ok 1 cpu0_online" \
		--log $(HOST_TMP)/ir0-selftest-arm64-sysfs-cpu.log
	@echo "✓ ir0-selftest-sysfs-cpu-arm64"

ir0-selftest-sysfs-cpu: ir0-selftest-sysfs-cpu-x86 ir0-selftest-sysfs-cpu-arm64

# SIGALTSTACK-SELFTEST
# mmap an alternate stack, sigaltstack(2), SA_ONSTACK on SIGUSR1. The
# handler's address must lie inside that mapping.
IR0_SELFTEST_X86_SIGALTSTACK := $(KERNEL_ROOT)/build/selftests/x86_64/sigaltstack
IR0_SELFTEST_ARM64_SIGALTSTACK := $(KERNEL_ROOT)/build/selftests/arm64/sigaltstack

.PHONY: ir0-selftest-sigaltstack-x86 ir0-selftest-sigaltstack-arm64 \
	ir0-selftest-sigaltstack

$(IR0_SELFTEST_X86_SIGALTSTACK): tests/selftests/syscall/sigaltstack.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 sigaltstack

$(IR0_SELFTEST_ARM64_SIGALTSTACK): tests/selftests/syscall/sigaltstack.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 sigaltstack

ir0-selftest-sigaltstack-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_SIGALTSTACK)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_SIGALTSTACK) \
		--done "ok 1 sigaltstack_on_alt" \
		--log $(HOST_TMP)/ir0-selftest-x86-sigaltstack.log
	@echo "✓ ir0-selftest-sigaltstack-x86"

ir0-selftest-sigaltstack-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_SIGALTSTACK)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_SIGALTSTACK) \
		--done "ok 1 sigaltstack_on_alt" \
		--log $(HOST_TMP)/ir0-selftest-arm64-sigaltstack.log
	@echo "✓ ir0-selftest-sigaltstack-arm64"

ir0-selftest-sigaltstack: ir0-selftest-sigaltstack-x86 ir0-selftest-sigaltstack-arm64

# SHM-EXIT-SELFTEST
IR0_SELFTEST_X86_SHM_EXIT := $(KERNEL_ROOT)/build/selftests/x86_64/shm_exit
IR0_SELFTEST_ARM64_SHM_EXIT := $(KERNEL_ROOT)/build/selftests/arm64/shm_exit

.PHONY: ir0-selftest-shm-exit-x86 ir0-selftest-shm-exit-arm64 ir0-selftest-shm-exit

$(IR0_SELFTEST_X86_SHM_EXIT): tests/selftests/syscall/shm_exit.c tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 shm_exit

$(IR0_SELFTEST_ARM64_SHM_EXIT): tests/selftests/syscall/shm_exit.c tests/selftests/ir0/ksft.h \
		tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 shm_exit

ir0-selftest-shm-exit-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_SHM_EXIT)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_SHM_EXIT) \
		--done "ok 1 shm_exit" \
		--log $(HOST_TMP)/ir0-selftest-x86-shm-exit.log
	@echo "✓ ir0-selftest-shm-exit-x86"

ir0-selftest-shm-exit-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_SHM_EXIT)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_SHM_EXIT) \
		--done "ok 1 shm_exit" \
		--log $(HOST_TMP)/ir0-selftest-arm64-shm-exit.log
	@echo "✓ ir0-selftest-shm-exit-arm64"

ir0-selftest-shm-exit: ir0-selftest-shm-exit-x86 ir0-selftest-shm-exit-arm64

# SHM-FORK-SELFTEST
# Parent shmat, fork, child stores at that address. The parent must see
# the child's byte: SysV shm is shared, not copy-on-write.
IR0_SELFTEST_X86_SHM_FORK := $(KERNEL_ROOT)/build/selftests/x86_64/shm_fork
IR0_SELFTEST_ARM64_SHM_FORK := $(KERNEL_ROOT)/build/selftests/arm64/shm_fork

.PHONY: ir0-selftest-shm-fork-x86 ir0-selftest-shm-fork-arm64 ir0-selftest-shm-fork

$(IR0_SELFTEST_X86_SHM_FORK): tests/selftests/syscall/shm_fork.c tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 shm_fork

$(IR0_SELFTEST_ARM64_SHM_FORK): tests/selftests/syscall/shm_fork.c tests/selftests/ir0/ksft.h \
		tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 shm_fork

ir0-selftest-shm-fork-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_SHM_FORK)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_SHM_FORK) \
		--done "ok 1 shm_fork" \
		--log $(HOST_TMP)/ir0-selftest-x86-shm-fork.log
	@echo "✓ ir0-selftest-shm-fork-x86"

ir0-selftest-shm-fork-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_SHM_FORK)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_SHM_FORK) \
		--done "ok 1 shm_fork" \
		--log $(HOST_TMP)/ir0-selftest-arm64-shm-fork.log
	@echo "✓ ir0-selftest-shm-fork-arm64"

ir0-selftest-shm-fork: ir0-selftest-shm-fork-x86 ir0-selftest-shm-fork-arm64

# PRCTL-TASK-SELFTEST
IR0_SELFTEST_X86_PRCTL_TASK := $(KERNEL_ROOT)/build/selftests/x86_64/prctl_task
IR0_SELFTEST_ARM64_PRCTL_TASK := $(KERNEL_ROOT)/build/selftests/arm64/prctl_task

.PHONY: ir0-selftest-prctl-task-x86 ir0-selftest-prctl-task-arm64 \
	ir0-selftest-prctl-task

$(IR0_SELFTEST_X86_PRCTL_TASK): tests/selftests/syscall/prctl_task.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 prctl_task

$(IR0_SELFTEST_ARM64_PRCTL_TASK): tests/selftests/syscall/prctl_task.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 prctl_task

ir0-selftest-prctl-task-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_PRCTL_TASK)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_PRCTL_TASK) \
		--done "ok 2 prctl_pdeathsig" \
		--log $(HOST_TMP)/ir0-selftest-x86-prctl-task.log
	@echo "✓ ir0-selftest-prctl-task-x86"

ir0-selftest-prctl-task-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_PRCTL_TASK)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_PRCTL_TASK) \
		--done "ok 2 prctl_pdeathsig" \
		--log $(HOST_TMP)/ir0-selftest-arm64-prctl-task.log
	@echo "✓ ir0-selftest-prctl-task-arm64"

ir0-selftest-prctl-task: ir0-selftest-prctl-task-x86 ir0-selftest-prctl-task-arm64

# SEGV-ACCERR-SELFTEST
IR0_SELFTEST_X86_SEGV_ACCERR := $(KERNEL_ROOT)/build/selftests/x86_64/segv_accerr
IR0_SELFTEST_ARM64_SEGV_ACCERR := $(KERNEL_ROOT)/build/selftests/arm64/segv_accerr

.PHONY: ir0-selftest-segv-accerr-x86 ir0-selftest-segv-accerr-arm64 \
	ir0-selftest-segv-accerr

$(IR0_SELFTEST_X86_SEGV_ACCERR): tests/selftests/syscall/segv_accerr.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 segv_accerr

$(IR0_SELFTEST_ARM64_SEGV_ACCERR): tests/selftests/syscall/segv_accerr.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 segv_accerr

ir0-selftest-segv-accerr-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_SEGV_ACCERR)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_SEGV_ACCERR) \
		--done "ok 1 segv_accerr" \
		--log $(HOST_TMP)/ir0-selftest-x86-segv-accerr.log
	@echo "✓ ir0-selftest-segv-accerr-x86"

ir0-selftest-segv-accerr-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_SEGV_ACCERR)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_SEGV_ACCERR) \
		--done "ok 1 segv_accerr" \
		--log $(HOST_TMP)/ir0-selftest-arm64-segv-accerr.log
	@echo "✓ ir0-selftest-segv-accerr-arm64"

ir0-selftest-segv-accerr: ir0-selftest-segv-accerr-x86 ir0-selftest-segv-accerr-arm64

# PROC-STATUS-SELFTEST
IR0_SELFTEST_X86_PROC_STATUS := $(KERNEL_ROOT)/build/selftests/x86_64/proc_status
IR0_SELFTEST_ARM64_PROC_STATUS := $(KERNEL_ROOT)/build/selftests/arm64/proc_status

.PHONY: ir0-selftest-proc-status-x86 ir0-selftest-proc-status-arm64 \
	ir0-selftest-proc-status

$(IR0_SELFTEST_X86_PROC_STATUS): tests/selftests/syscall/proc_status.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 proc_status

$(IR0_SELFTEST_ARM64_PROC_STATUS): tests/selftests/syscall/proc_status.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 proc_status

ir0-selftest-proc-status-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_PROC_STATUS)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_PROC_STATUS) \
		--done "ok 1 proc_status" \
		--log $(HOST_TMP)/ir0-selftest-x86-proc-status.log
	@echo "✓ ir0-selftest-proc-status-x86"

ir0-selftest-proc-status-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_PROC_STATUS)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_PROC_STATUS) \
		--done "ok 1 proc_status" \
		--log $(HOST_TMP)/ir0-selftest-arm64-proc-status.log
	@echo "✓ ir0-selftest-proc-status-arm64"

ir0-selftest-proc-status: ir0-selftest-proc-status-x86 ir0-selftest-proc-status-arm64

# SHM-CPID-SELFTEST
IR0_SELFTEST_X86_SHM_CPID := $(KERNEL_ROOT)/build/selftests/x86_64/shm_cpid
IR0_SELFTEST_ARM64_SHM_CPID := $(KERNEL_ROOT)/build/selftests/arm64/shm_cpid

.PHONY: ir0-selftest-shm-cpid-x86 ir0-selftest-shm-cpid-arm64 \
	ir0-selftest-shm-cpid

$(IR0_SELFTEST_X86_SHM_CPID): tests/selftests/syscall/shm_cpid.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 shm_cpid

$(IR0_SELFTEST_ARM64_SHM_CPID): tests/selftests/syscall/shm_cpid.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 shm_cpid

ir0-selftest-shm-cpid-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_SHM_CPID)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_SHM_CPID) \
		--done "ok 1 shm_cpid" \
		--log $(HOST_TMP)/ir0-selftest-x86-shm-cpid.log
	@echo "✓ ir0-selftest-shm-cpid-x86"

ir0-selftest-shm-cpid-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_SHM_CPID)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_SHM_CPID) \
		--done "ok 1 shm_cpid" \
		--log $(HOST_TMP)/ir0-selftest-arm64-shm-cpid.log
	@echo "✓ ir0-selftest-shm-cpid-arm64"

ir0-selftest-shm-cpid: ir0-selftest-shm-cpid-x86 ir0-selftest-shm-cpid-arm64

# PRCTL-DUMPABLE-SELFTEST
IR0_SELFTEST_X86_PRCTL_DUMPABLE := $(KERNEL_ROOT)/build/selftests/x86_64/prctl_dumpable
IR0_SELFTEST_ARM64_PRCTL_DUMPABLE := $(KERNEL_ROOT)/build/selftests/arm64/prctl_dumpable

.PHONY: ir0-selftest-prctl-dumpable-x86 ir0-selftest-prctl-dumpable-arm64 \
	ir0-selftest-prctl-dumpable

$(IR0_SELFTEST_X86_PRCTL_DUMPABLE): tests/selftests/syscall/prctl_dumpable.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 prctl_dumpable

$(IR0_SELFTEST_ARM64_PRCTL_DUMPABLE): tests/selftests/syscall/prctl_dumpable.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 prctl_dumpable

ir0-selftest-prctl-dumpable-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_PRCTL_DUMPABLE)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_PRCTL_DUMPABLE) \
		--done "ok 1 prctl_dumpable" \
		--log $(HOST_TMP)/ir0-selftest-x86-prctl-dumpable.log
	@echo "✓ ir0-selftest-prctl-dumpable-x86"

ir0-selftest-prctl-dumpable-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_PRCTL_DUMPABLE)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_PRCTL_DUMPABLE) \
		--done "ok 1 prctl_dumpable" \
		--log $(HOST_TMP)/ir0-selftest-arm64-prctl-dumpable.log
	@echo "✓ ir0-selftest-prctl-dumpable-arm64"

ir0-selftest-prctl-dumpable: ir0-selftest-prctl-dumpable-x86 ir0-selftest-prctl-dumpable-arm64

# SHM-LPID-SELFTEST
IR0_SELFTEST_X86_SHM_LPID := $(KERNEL_ROOT)/build/selftests/x86_64/shm_lpid
IR0_SELFTEST_ARM64_SHM_LPID := $(KERNEL_ROOT)/build/selftests/arm64/shm_lpid

.PHONY: ir0-selftest-shm-lpid-x86 ir0-selftest-shm-lpid-arm64 \
	ir0-selftest-shm-lpid

$(IR0_SELFTEST_X86_SHM_LPID): tests/selftests/syscall/shm_lpid.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 shm_lpid

$(IR0_SELFTEST_ARM64_SHM_LPID): tests/selftests/syscall/shm_lpid.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 shm_lpid

ir0-selftest-shm-lpid-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_SHM_LPID)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_SHM_LPID) \
		--done "ok 1 shm_lpid" \
		--log $(HOST_TMP)/ir0-selftest-x86-shm-lpid.log
	@echo "✓ ir0-selftest-shm-lpid-x86"

ir0-selftest-shm-lpid-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_SHM_LPID)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_SHM_LPID) \
		--done "ok 1 shm_lpid" \
		--log $(HOST_TMP)/ir0-selftest-arm64-shm-lpid.log
	@echo "✓ ir0-selftest-shm-lpid-arm64"

ir0-selftest-shm-lpid: ir0-selftest-shm-lpid-x86 ir0-selftest-shm-lpid-arm64

# PRCTL-KEEPCAPS-SELFTEST
IR0_SELFTEST_X86_PRCTL_KEEPCAPS := $(KERNEL_ROOT)/build/selftests/x86_64/prctl_keepcaps
IR0_SELFTEST_ARM64_PRCTL_KEEPCAPS := $(KERNEL_ROOT)/build/selftests/arm64/prctl_keepcaps

.PHONY: ir0-selftest-prctl-keepcaps-x86 ir0-selftest-prctl-keepcaps-arm64 \
	ir0-selftest-prctl-keepcaps

$(IR0_SELFTEST_X86_PRCTL_KEEPCAPS): tests/selftests/syscall/prctl_keepcaps.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 prctl_keepcaps

$(IR0_SELFTEST_ARM64_PRCTL_KEEPCAPS): tests/selftests/syscall/prctl_keepcaps.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 prctl_keepcaps

ir0-selftest-prctl-keepcaps-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_PRCTL_KEEPCAPS)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_PRCTL_KEEPCAPS) \
		--done "ok 1 prctl_keepcaps" \
		--log $(HOST_TMP)/ir0-selftest-x86-prctl-keepcaps.log
	@echo "✓ ir0-selftest-prctl-keepcaps-x86"

ir0-selftest-prctl-keepcaps-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_PRCTL_KEEPCAPS)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_PRCTL_KEEPCAPS) \
		--done "ok 1 prctl_keepcaps" \
		--log $(HOST_TMP)/ir0-selftest-arm64-prctl-keepcaps.log
	@echo "✓ ir0-selftest-prctl-keepcaps-arm64"

ir0-selftest-prctl-keepcaps: ir0-selftest-prctl-keepcaps-x86 ir0-selftest-prctl-keepcaps-arm64

# SHM-TIMES-SELFTEST
IR0_SELFTEST_X86_SHM_TIMES := $(KERNEL_ROOT)/build/selftests/x86_64/shm_times
IR0_SELFTEST_ARM64_SHM_TIMES := $(KERNEL_ROOT)/build/selftests/arm64/shm_times

.PHONY: ir0-selftest-shm-times-x86 ir0-selftest-shm-times-arm64 \
	ir0-selftest-shm-times

$(IR0_SELFTEST_X86_SHM_TIMES): tests/selftests/syscall/shm_times.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 shm_times

$(IR0_SELFTEST_ARM64_SHM_TIMES): tests/selftests/syscall/shm_times.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 shm_times

ir0-selftest-shm-times-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_SHM_TIMES)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_SHM_TIMES) \
		--done "ok 1 shm_times" \
		--log $(HOST_TMP)/ir0-selftest-x86-shm-times.log
	@echo "✓ ir0-selftest-shm-times-x86"

ir0-selftest-shm-times-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_SHM_TIMES)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_SHM_TIMES) \
		--done "ok 1 shm_times" \
		--log $(HOST_TMP)/ir0-selftest-arm64-shm-times.log
	@echo "✓ ir0-selftest-shm-times-arm64"

ir0-selftest-shm-times: ir0-selftest-shm-times-x86 ir0-selftest-shm-times-arm64

# SHM-OWNER-SELFTEST
IR0_SELFTEST_X86_SHM_OWNER := $(KERNEL_ROOT)/build/selftests/x86_64/shm_owner
IR0_SELFTEST_ARM64_SHM_OWNER := $(KERNEL_ROOT)/build/selftests/arm64/shm_owner

.PHONY: ir0-selftest-shm-owner-x86 ir0-selftest-shm-owner-arm64 \
	ir0-selftest-shm-owner

$(IR0_SELFTEST_X86_SHM_OWNER): tests/selftests/syscall/shm_owner.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 shm_owner

$(IR0_SELFTEST_ARM64_SHM_OWNER): tests/selftests/syscall/shm_owner.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 shm_owner

ir0-selftest-shm-owner-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_SHM_OWNER)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_SHM_OWNER) \
		--done "ok 1 shm_owner" \
		--log $(HOST_TMP)/ir0-selftest-x86-shm-owner.log
	@echo "✓ ir0-selftest-shm-owner-x86"

ir0-selftest-shm-owner-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_SHM_OWNER)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_SHM_OWNER) \
		--done "ok 1 shm_owner" \
		--log $(HOST_TMP)/ir0-selftest-arm64-shm-owner.log
	@echo "✓ ir0-selftest-shm-owner-arm64"

ir0-selftest-shm-owner: ir0-selftest-shm-owner-x86 ir0-selftest-shm-owner-arm64

# PROC-UMASK-SELFTEST
IR0_SELFTEST_X86_PROC_UMASK := $(KERNEL_ROOT)/build/selftests/x86_64/proc_umask
IR0_SELFTEST_ARM64_PROC_UMASK := $(KERNEL_ROOT)/build/selftests/arm64/proc_umask

.PHONY: ir0-selftest-proc-umask-x86 ir0-selftest-proc-umask-arm64 \
	ir0-selftest-proc-umask

$(IR0_SELFTEST_X86_PROC_UMASK): tests/selftests/syscall/proc_umask.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 proc_umask

$(IR0_SELFTEST_ARM64_PROC_UMASK): tests/selftests/syscall/proc_umask.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 proc_umask

ir0-selftest-proc-umask-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_PROC_UMASK)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_PROC_UMASK) \
		--done "ok 1 proc_umask" \
		--log $(HOST_TMP)/ir0-selftest-x86-proc-umask.log
	@echo "✓ ir0-selftest-proc-umask-x86"

ir0-selftest-proc-umask-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_PROC_UMASK)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_PROC_UMASK) \
		--done "ok 1 proc_umask" \
		--log $(HOST_TMP)/ir0-selftest-arm64-proc-umask.log
	@echo "✓ ir0-selftest-proc-umask-arm64"

ir0-selftest-proc-umask: ir0-selftest-proc-umask-x86 ir0-selftest-proc-umask-arm64

# SHM-MODE-SELFTEST
IR0_SELFTEST_X86_SHM_MODE := $(KERNEL_ROOT)/build/selftests/x86_64/shm_mode
IR0_SELFTEST_ARM64_SHM_MODE := $(KERNEL_ROOT)/build/selftests/arm64/shm_mode

.PHONY: ir0-selftest-shm-mode-x86 ir0-selftest-shm-mode-arm64 \
	ir0-selftest-shm-mode

$(IR0_SELFTEST_X86_SHM_MODE): tests/selftests/syscall/shm_mode.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 shm_mode

$(IR0_SELFTEST_ARM64_SHM_MODE): tests/selftests/syscall/shm_mode.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 shm_mode

ir0-selftest-shm-mode-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_SHM_MODE)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_SHM_MODE) \
		--done "ok 1 shm_mode" \
		--log $(HOST_TMP)/ir0-selftest-x86-shm-mode.log
	@echo "✓ ir0-selftest-shm-mode-x86"

ir0-selftest-shm-mode-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_SHM_MODE)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_SHM_MODE) \
		--done "ok 1 shm_mode" \
		--log $(HOST_TMP)/ir0-selftest-arm64-shm-mode.log
	@echo "✓ ir0-selftest-shm-mode-arm64"

ir0-selftest-shm-mode: ir0-selftest-shm-mode-x86 ir0-selftest-shm-mode-arm64

# PROC-SIGSET-SELFTEST
IR0_SELFTEST_X86_PROC_SIGSET := $(KERNEL_ROOT)/build/selftests/x86_64/proc_sigset
IR0_SELFTEST_ARM64_PROC_SIGSET := $(KERNEL_ROOT)/build/selftests/arm64/proc_sigset

.PHONY: ir0-selftest-proc-sigset-x86 ir0-selftest-proc-sigset-arm64 \
	ir0-selftest-proc-sigset

$(IR0_SELFTEST_X86_PROC_SIGSET): tests/selftests/syscall/proc_sigset.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 proc_sigset

$(IR0_SELFTEST_ARM64_PROC_SIGSET): tests/selftests/syscall/proc_sigset.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 proc_sigset

ir0-selftest-proc-sigset-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_PROC_SIGSET)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_PROC_SIGSET) \
		--done "ok 1 proc_sigset" \
		--log $(HOST_TMP)/ir0-selftest-x86-proc-sigset.log
	@echo "✓ ir0-selftest-proc-sigset-x86"

ir0-selftest-proc-sigset-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_PROC_SIGSET)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_PROC_SIGSET) \
		--done "ok 1 proc_sigset" \
		--log $(HOST_TMP)/ir0-selftest-arm64-proc-sigset.log
	@echo "✓ ir0-selftest-proc-sigset-arm64"

ir0-selftest-proc-sigset: ir0-selftest-proc-sigset-x86 ir0-selftest-proc-sigset-arm64

# ILL-CODE-SELFTEST
IR0_SELFTEST_X86_ILL_CODE := $(KERNEL_ROOT)/build/selftests/x86_64/ill_code
IR0_SELFTEST_ARM64_ILL_CODE := $(KERNEL_ROOT)/build/selftests/arm64/ill_code

.PHONY: ir0-selftest-ill-code-x86 ir0-selftest-ill-code-arm64 \
	ir0-selftest-ill-code

$(IR0_SELFTEST_X86_ILL_CODE): tests/selftests/syscall/ill_code.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 ill_code

$(IR0_SELFTEST_ARM64_ILL_CODE): tests/selftests/syscall/ill_code.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 ill_code

ir0-selftest-ill-code-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_ILL_CODE)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_ILL_CODE) \
		--done "ok 1 ill_code" \
		--log $(HOST_TMP)/ir0-selftest-x86-ill-code.log
	@echo "✓ ir0-selftest-ill-code-x86"

ir0-selftest-ill-code-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_ILL_CODE)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_ILL_CODE) \
		--done "ok 1 ill_code" \
		--log $(HOST_TMP)/ir0-selftest-arm64-ill-code.log
	@echo "✓ ir0-selftest-ill-code-arm64"

ir0-selftest-ill-code: ir0-selftest-ill-code-x86 ir0-selftest-ill-code-arm64

# PROC-SIGACT-SELFTEST
IR0_SELFTEST_X86_PROC_SIGACT := $(KERNEL_ROOT)/build/selftests/x86_64/proc_sigact
IR0_SELFTEST_ARM64_PROC_SIGACT := $(KERNEL_ROOT)/build/selftests/arm64/proc_sigact

.PHONY: ir0-selftest-proc-sigact-x86 ir0-selftest-proc-sigact-arm64 \
	ir0-selftest-proc-sigact

$(IR0_SELFTEST_X86_PROC_SIGACT): tests/selftests/syscall/proc_sigact.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 proc_sigact

$(IR0_SELFTEST_ARM64_PROC_SIGACT): tests/selftests/syscall/proc_sigact.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 proc_sigact

ir0-selftest-proc-sigact-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_PROC_SIGACT)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_PROC_SIGACT) \
		--done "ok 1 proc_sigact" \
		--log $(HOST_TMP)/ir0-selftest-x86-proc-sigact.log
	@echo "✓ ir0-selftest-proc-sigact-x86"

ir0-selftest-proc-sigact-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_PROC_SIGACT)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_PROC_SIGACT) \
		--done "ok 1 proc_sigact" \
		--log $(HOST_TMP)/ir0-selftest-arm64-proc-sigact.log
	@echo "✓ ir0-selftest-proc-sigact-arm64"

ir0-selftest-proc-sigact: ir0-selftest-proc-sigact-x86 ir0-selftest-proc-sigact-arm64

# PROC-GROUPS-SELFTEST
IR0_SELFTEST_X86_PROC_GROUPS := $(KERNEL_ROOT)/build/selftests/x86_64/proc_groups
IR0_SELFTEST_ARM64_PROC_GROUPS := $(KERNEL_ROOT)/build/selftests/arm64/proc_groups

.PHONY: ir0-selftest-proc-groups-x86 ir0-selftest-proc-groups-arm64 \
	ir0-selftest-proc-groups

$(IR0_SELFTEST_X86_PROC_GROUPS): tests/selftests/syscall/proc_groups.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 proc_groups

$(IR0_SELFTEST_ARM64_PROC_GROUPS): tests/selftests/syscall/proc_groups.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 proc_groups

ir0-selftest-proc-groups-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_PROC_GROUPS)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_PROC_GROUPS) \
		--done "ok 1 proc_groups" \
		--log $(HOST_TMP)/ir0-selftest-x86-proc-groups.log
	@echo "✓ ir0-selftest-proc-groups-x86"

ir0-selftest-proc-groups-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_PROC_GROUPS)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_PROC_GROUPS) \
		--done "ok 1 proc_groups" \
		--log $(HOST_TMP)/ir0-selftest-arm64-proc-groups.log
	@echo "✓ ir0-selftest-proc-groups-arm64"

ir0-selftest-proc-groups: ir0-selftest-proc-groups-x86 ir0-selftest-proc-groups-arm64

# PIPE-SZ-SELFTEST
IR0_SELFTEST_X86_PIPE_SZ := $(KERNEL_ROOT)/build/selftests/x86_64/pipe_sz
IR0_SELFTEST_ARM64_PIPE_SZ := $(KERNEL_ROOT)/build/selftests/arm64/pipe_sz

.PHONY: ir0-selftest-pipe-sz-x86 ir0-selftest-pipe-sz-arm64 ir0-selftest-pipe-sz

$(IR0_SELFTEST_X86_PIPE_SZ): tests/selftests/syscall/pipe_sz.c tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 pipe_sz

$(IR0_SELFTEST_ARM64_PIPE_SZ): tests/selftests/syscall/pipe_sz.c tests/selftests/ir0/ksft.h \
		tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 pipe_sz

ir0-selftest-pipe-sz-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_PIPE_SZ)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_PIPE_SZ) \
		--done "ok 1 pipe_sz" \
		--log $(HOST_TMP)/ir0-selftest-x86-pipe-sz.log
	@echo "✓ ir0-selftest-pipe-sz-x86"

ir0-selftest-pipe-sz-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_PIPE_SZ)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_PIPE_SZ) \
		--done "ok 1 pipe_sz" \
		--log $(HOST_TMP)/ir0-selftest-arm64-pipe-sz.log
	@echo "✓ ir0-selftest-pipe-sz-arm64"

ir0-selftest-pipe-sz: ir0-selftest-pipe-sz-x86 ir0-selftest-pipe-sz-arm64

# WAITID-SELFTEST
IR0_SELFTEST_X86_WAITID := $(KERNEL_ROOT)/build/selftests/x86_64/waitid
IR0_SELFTEST_ARM64_WAITID := $(KERNEL_ROOT)/build/selftests/arm64/waitid

.PHONY: ir0-selftest-waitid-x86 ir0-selftest-waitid-arm64 \
	ir0-selftest-waitid

$(IR0_SELFTEST_X86_WAITID): tests/selftests/syscall/waitid.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 waitid

$(IR0_SELFTEST_ARM64_WAITID): tests/selftests/syscall/waitid.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 waitid

ir0-selftest-waitid-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_WAITID)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_WAITID) \
		--done "ok 1 waitid" \
		--log $(HOST_TMP)/ir0-selftest-x86-waitid.log
	@echo "✓ ir0-selftest-waitid-x86"

ir0-selftest-waitid-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_WAITID)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_WAITID) \
		--done "ok 1 waitid" \
		--log $(HOST_TMP)/ir0-selftest-arm64-waitid.log
	@echo "✓ ir0-selftest-waitid-arm64"

ir0-selftest-waitid: ir0-selftest-waitid-x86 ir0-selftest-waitid-arm64

# KD-KEYMAP-SELFTEST
IR0_SELFTEST_X86_KD_KEYMAP := $(KERNEL_ROOT)/build/selftests/x86_64/kd_keymap
IR0_SELFTEST_ARM64_KD_KEYMAP := $(KERNEL_ROOT)/build/selftests/arm64/kd_keymap

.PHONY: ir0-selftest-kd-keymap-x86 ir0-selftest-kd-keymap-arm64 \
	ir0-selftest-kd-keymap

$(IR0_SELFTEST_X86_KD_KEYMAP): tests/selftests/syscall/kd_keymap.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 kd_keymap

$(IR0_SELFTEST_ARM64_KD_KEYMAP): tests/selftests/syscall/kd_keymap.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 kd_keymap

ir0-selftest-kd-keymap-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_KD_KEYMAP)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_KD_KEYMAP) \
		--done "ok 1 kd_keymap" \
		--log $(HOST_TMP)/ir0-selftest-x86-kd-keymap.log
	@echo "✓ ir0-selftest-kd-keymap-x86"

ir0-selftest-kd-keymap-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_KD_KEYMAP)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_KD_KEYMAP) \
		--done "ok 1 kd_keymap" \
		--log $(HOST_TMP)/ir0-selftest-arm64-kd-keymap.log
	@echo "✓ ir0-selftest-kd-keymap-arm64"

ir0-selftest-kd-keymap: ir0-selftest-kd-keymap-x86 ir0-selftest-kd-keymap-arm64

# PROC-COMM-SELFTEST
IR0_SELFTEST_X86_PROC_COMM := $(KERNEL_ROOT)/build/selftests/x86_64/proc_comm
IR0_SELFTEST_ARM64_PROC_COMM := $(KERNEL_ROOT)/build/selftests/arm64/proc_comm

.PHONY: ir0-selftest-proc-comm-x86 ir0-selftest-proc-comm-arm64 \
	ir0-selftest-proc-comm

$(IR0_SELFTEST_X86_PROC_COMM): tests/selftests/syscall/proc_comm.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 proc_comm

$(IR0_SELFTEST_ARM64_PROC_COMM): tests/selftests/syscall/proc_comm.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 proc_comm

ir0-selftest-proc-comm-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_PROC_COMM)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_PROC_COMM) \
		--done "ok 1 proc_comm" \
		--log $(HOST_TMP)/ir0-selftest-x86-proc-comm.log
	@echo "✓ ir0-selftest-proc-comm-x86"

ir0-selftest-proc-comm-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_PROC_COMM)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_PROC_COMM) \
		--done "ok 1 proc_comm" \
		--log $(HOST_TMP)/ir0-selftest-arm64-proc-comm.log
	@echo "✓ ir0-selftest-proc-comm-arm64"

ir0-selftest-proc-comm: ir0-selftest-proc-comm-x86 ir0-selftest-proc-comm-arm64

# SHM-RDONLY-SELFTEST
IR0_SELFTEST_X86_SHM_RDONLY := $(KERNEL_ROOT)/build/selftests/x86_64/shm_rdonly
IR0_SELFTEST_ARM64_SHM_RDONLY := $(KERNEL_ROOT)/build/selftests/arm64/shm_rdonly

.PHONY: ir0-selftest-shm-rdonly-x86 ir0-selftest-shm-rdonly-arm64 \
	ir0-selftest-shm-rdonly

$(IR0_SELFTEST_X86_SHM_RDONLY): tests/selftests/syscall/shm_rdonly.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 shm_rdonly

$(IR0_SELFTEST_ARM64_SHM_RDONLY): tests/selftests/syscall/shm_rdonly.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 shm_rdonly

ir0-selftest-shm-rdonly-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_SHM_RDONLY)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_SHM_RDONLY) \
		--done "ok 1 shm_rdonly" \
		--log $(HOST_TMP)/ir0-selftest-x86-shm-rdonly.log
	@echo "✓ ir0-selftest-shm-rdonly-x86"

ir0-selftest-shm-rdonly-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_SHM_RDONLY)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_SHM_RDONLY) \
		--done "ok 1 shm_rdonly" \
		--log $(HOST_TMP)/ir0-selftest-arm64-shm-rdonly.log
	@echo "✓ ir0-selftest-shm-rdonly-arm64"

ir0-selftest-shm-rdonly: ir0-selftest-shm-rdonly-x86 ir0-selftest-shm-rdonly-arm64

# ELF-INTERP-SELFTEST
IR0_SELFTEST_X86_ELF_INTERP := $(KERNEL_ROOT)/build/selftests/x86_64/elf_interp
IR0_SELFTEST_ARM64_ELF_INTERP := $(KERNEL_ROOT)/build/selftests/arm64/elf_interp

.PHONY: ir0-selftest-elf-interp-x86 ir0-selftest-elf-interp-arm64 \
	ir0-selftest-elf-interp

$(IR0_SELFTEST_X86_ELF_INTERP): tests/selftests/syscall/elf_interp.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 elf_interp

$(IR0_SELFTEST_ARM64_ELF_INTERP): tests/selftests/syscall/elf_interp.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 elf_interp

ir0-selftest-elf-interp-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_ELF_INTERP)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_ELF_INTERP) \
		--done "ok 1 elf_interp" \
		--log $(HOST_TMP)/ir0-selftest-x86-elf-interp.log
	@echo "✓ ir0-selftest-elf-interp-x86"

ir0-selftest-elf-interp-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_ELF_INTERP)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_ELF_INTERP) \
		--done "ok 1 elf_interp" \
		--log $(HOST_TMP)/ir0-selftest-arm64-elf-interp.log
	@echo "✓ ir0-selftest-elf-interp-arm64"

ir0-selftest-elf-interp: ir0-selftest-elf-interp-x86 ir0-selftest-elf-interp-arm64

# DYN-HELLO-SELFTEST
# The first dynamically linked program (tests/selftests/syscall/dyn_hello.c):
# execve loads PT_INTERP /lib/ld-musl-<arch>.so.1, which maps libc and
# /lib/libdynhello.so MAP_PRIVATE and runs their constructors. MINIX v1
# names stop at 14 characters: the static dyn_hello_stage (PID 1) copies
# the files from /dyn into a tmpfs /lib first.
IR0_DYN_HELLO_X86 := $(KERNEL_ROOT)/build/selftests/x86_64
IR0_DYN_HELLO_ARM64 := $(KERNEL_ROOT)/build/selftests/arm64

.PHONY: ir0-selftest-dyn-hello-x86 ir0-selftest-dyn-hello-arm64 \
	ir0-selftest-dyn-hello

ir0-selftest-dyn-hello-x86: kernel-x64-userspace.iso
	@$(MAKE) -C tests/selftests ARCH=x86_64 dyn_hello
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_DYN_HELLO_X86)/dyn_hello_stage \
		--extra $(IR0_DYN_HELLO_X86)/dyn_hello:dyn/hello \
		--extra $(IR0_DYN_HELLO_X86)/ld-musl-x86_64.so.1:dyn/ldso \
		--extra $(IR0_DYN_HELLO_X86)/libdynhello.so:dyn/libdynhello.so \
		--done "ok 3 dynamic_hello" \
		--log $(HOST_TMP)/ir0-selftest-x86-dyn-hello.log
	@echo "✓ ir0-selftest-dyn-hello-x86"

ir0-selftest-dyn-hello-arm64: kernel-arm64-product.bin
	@$(MAKE) -C tests/selftests ARCH=arm64 dyn_hello
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_DYN_HELLO_ARM64)/dyn_hello_stage \
		--extra $(IR0_DYN_HELLO_ARM64)/dyn_hello:dyn/hello \
		--extra $(IR0_DYN_HELLO_ARM64)/ld-musl-aarch64.so.1:dyn/ldso \
		--extra $(IR0_DYN_HELLO_ARM64)/libdynhello.so:dyn/libdynhello.so \
		--done "ok 3 dynamic_hello" \
		--log $(HOST_TMP)/ir0-selftest-arm64-dyn-hello.log
	@echo "✓ ir0-selftest-dyn-hello-arm64"

ir0-selftest-dyn-hello: ir0-selftest-dyn-hello-x86 ir0-selftest-dyn-hello-arm64

# EXEC-SIGMASK-SELFTEST
IR0_SELFTEST_X86_EXEC_SIGMASK := $(KERNEL_ROOT)/build/selftests/x86_64/exec_sigmask
IR0_SELFTEST_ARM64_EXEC_SIGMASK := $(KERNEL_ROOT)/build/selftests/arm64/exec_sigmask

.PHONY: ir0-selftest-exec-sigmask-x86 ir0-selftest-exec-sigmask-arm64 \
	ir0-selftest-exec-sigmask

$(IR0_SELFTEST_X86_EXEC_SIGMASK): tests/selftests/syscall/exec_sigmask.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 exec_sigmask

$(IR0_SELFTEST_ARM64_EXEC_SIGMASK): tests/selftests/syscall/exec_sigmask.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 exec_sigmask

ir0-selftest-exec-sigmask-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_EXEC_SIGMASK)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_EXEC_SIGMASK) \
		--done "ok 1 exec_sigmask" \
		--log $(HOST_TMP)/ir0-selftest-x86-exec-sigmask.log
	@echo "✓ ir0-selftest-exec-sigmask-x86"

ir0-selftest-exec-sigmask-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_EXEC_SIGMASK)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_EXEC_SIGMASK) \
		--done "ok 1 exec_sigmask" \
		--log $(HOST_TMP)/ir0-selftest-arm64-exec-sigmask.log
	@echo "✓ ir0-selftest-exec-sigmask-arm64"

ir0-selftest-exec-sigmask: ir0-selftest-exec-sigmask-x86 ir0-selftest-exec-sigmask-arm64

# GETDENTS-LAYOUT-SELFTEST
IR0_SELFTEST_X86_GETDENTS_LAYOUT := $(KERNEL_ROOT)/build/selftests/x86_64/getdents_layout
IR0_SELFTEST_ARM64_GETDENTS_LAYOUT := $(KERNEL_ROOT)/build/selftests/arm64/getdents_layout

.PHONY: ir0-selftest-getdents-layout-x86 ir0-selftest-getdents-layout-arm64 \
	ir0-selftest-getdents-layout

$(IR0_SELFTEST_X86_GETDENTS_LAYOUT): tests/selftests/syscall/getdents_layout.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 getdents_layout

$(IR0_SELFTEST_ARM64_GETDENTS_LAYOUT): tests/selftests/syscall/getdents_layout.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 getdents_layout

ir0-selftest-getdents-layout-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_GETDENTS_LAYOUT)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_GETDENTS_LAYOUT) \
		--done "ok 1 getdents_layout" \
		--log $(HOST_TMP)/ir0-selftest-x86-getdents-layout.log
	@echo "✓ ir0-selftest-getdents-layout-x86"

ir0-selftest-getdents-layout-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_GETDENTS_LAYOUT)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_GETDENTS_LAYOUT) \
		--done "ok 1 getdents_layout" \
		--log $(HOST_TMP)/ir0-selftest-arm64-getdents-layout.log
	@echo "✓ ir0-selftest-getdents-layout-arm64"

ir0-selftest-getdents-layout: ir0-selftest-getdents-layout-x86 ir0-selftest-getdents-layout-arm64

# NAMEI-SEARCH-SELFTEST
IR0_SELFTEST_X86_NAMEI_SEARCH := $(KERNEL_ROOT)/build/selftests/x86_64/namei_search
IR0_SELFTEST_ARM64_NAMEI_SEARCH := $(KERNEL_ROOT)/build/selftests/arm64/namei_search

.PHONY: ir0-selftest-namei-search-x86 ir0-selftest-namei-search-arm64 \
	ir0-selftest-namei-search

$(IR0_SELFTEST_X86_NAMEI_SEARCH): tests/selftests/syscall/namei_search.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 namei_search

$(IR0_SELFTEST_ARM64_NAMEI_SEARCH): tests/selftests/syscall/namei_search.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 namei_search

ir0-selftest-namei-search-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_NAMEI_SEARCH)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_NAMEI_SEARCH) \
		--done "ok 1 namei_search" \
		--log $(HOST_TMP)/ir0-selftest-x86-namei-search.log
	@echo "✓ ir0-selftest-namei-search-x86"

ir0-selftest-namei-search-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_NAMEI_SEARCH)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_NAMEI_SEARCH) \
		--done "ok 1 namei_search" \
		--log $(HOST_TMP)/ir0-selftest-arm64-namei-search.log
	@echo "✓ ir0-selftest-namei-search-arm64"

ir0-selftest-namei-search: ir0-selftest-namei-search-x86 ir0-selftest-namei-search-arm64

# BIGMEM-SELFTEST
IR0_SELFTEST_X86_BIGMEM := $(KERNEL_ROOT)/build/selftests/x86_64/bigmem
IR0_SELFTEST_ARM64_BIGMEM := $(KERNEL_ROOT)/build/selftests/arm64/bigmem

.PHONY: ir0-selftest-bigmem-x86 ir0-selftest-bigmem-arm64 \
	ir0-selftest-bigmem

$(IR0_SELFTEST_X86_BIGMEM): tests/selftests/mm/bigmem.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 bigmem

$(IR0_SELFTEST_ARM64_BIGMEM): tests/selftests/mm/bigmem.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 bigmem

ir0-selftest-bigmem-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_BIGMEM)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_BIGMEM) \
		--mem 3584M \
		--timeout 300 \
		--stale-sec 240 \
		--done "ok 2 touch_1280_mib_anon" \
		--expect-log "ok 1 memtotal_reflects_ram" \
		--log $(HOST_TMP)/ir0-selftest-x86-bigmem.log
	@echo "✓ ir0-selftest-bigmem-x86"

ir0-selftest-bigmem-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_BIGMEM)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_BIGMEM) \
		--mem 3584M \
		--timeout 300 \
		--stale-sec 240 \
		--done "ok 2 touch_1280_mib_anon" \
		--expect-log "ok 1 memtotal_reflects_ram" \
		--log $(HOST_TMP)/ir0-selftest-arm64-bigmem.log
	@echo "✓ ir0-selftest-bigmem-arm64"

ir0-selftest-bigmem: ir0-selftest-bigmem-x86 ir0-selftest-bigmem-arm64

# SCHED-NICE-SELFTEST
IR0_SELFTEST_X86_SCHED_NICE := $(KERNEL_ROOT)/build/selftests/x86_64/sched_nice
IR0_SELFTEST_ARM64_SCHED_NICE := $(KERNEL_ROOT)/build/selftests/arm64/sched_nice

.PHONY: ir0-selftest-sched-nice-x86 ir0-selftest-sched-nice-arm64 \
	ir0-selftest-sched-nice

$(IR0_SELFTEST_X86_SCHED_NICE): tests/selftests/syscall/sched_nice.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 sched_nice

$(IR0_SELFTEST_ARM64_SCHED_NICE): tests/selftests/syscall/sched_nice.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 sched_nice

ir0-selftest-sched-nice-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_SCHED_NICE)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_SCHED_NICE) \
		--mem 256M \
		--timeout 60 \
		--stale-sec 45 \
		--done "ok 1 sched_nice" \
		--log $(HOST_TMP)/ir0-selftest-x86-sched-nice.log
	@echo "✓ ir0-selftest-sched-nice-x86"

ir0-selftest-sched-nice-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_SCHED_NICE)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_SCHED_NICE) \
		--mem 256M \
		--timeout 60 \
		--stale-sec 45 \
		--done "ok 1 sched_nice" \
		--log $(HOST_TMP)/ir0-selftest-arm64-sched-nice.log
	@echo "✓ ir0-selftest-sched-nice-arm64"

ir0-selftest-sched-nice: ir0-selftest-sched-nice-x86 ir0-selftest-sched-nice-arm64

# SCHED-LAT-SELFTEST
IR0_SELFTEST_X86_SCHED_LAT := $(KERNEL_ROOT)/build/selftests/x86_64/sched_lat
IR0_SELFTEST_ARM64_SCHED_LAT := $(KERNEL_ROOT)/build/selftests/arm64/sched_lat

.PHONY: ir0-selftest-sched-lat-x86 ir0-selftest-sched-lat-arm64 \
	ir0-selftest-sched-lat

$(IR0_SELFTEST_X86_SCHED_LAT): tests/selftests/syscall/sched_lat.c \
		tests/selftests/ir0/ksft.h
	@$(MAKE) -C tests/selftests ARCH=x86_64 sched_lat

$(IR0_SELFTEST_ARM64_SCHED_LAT): tests/selftests/syscall/sched_lat.c \
		tests/selftests/ir0/ksft.h tests/selftests/ir0/nolibc.h
	@$(MAKE) -C tests/selftests ARCH=arm64 sched_lat

ir0-selftest-sched-lat-x86: kernel-x64-userspace.iso $(IR0_SELFTEST_X86_SCHED_LAT)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch x86_64 \
		--binary $(IR0_SELFTEST_X86_SCHED_LAT) \
		--done "ok 1 sched_lat" \
		--log $(HOST_TMP)/ir0-selftest-x86-sched-lat.log
	@echo "✓ ir0-selftest-sched-lat-x86"

ir0-selftest-sched-lat-arm64: kernel-arm64-product.bin $(IR0_SELFTEST_ARM64_SCHED_LAT)
	@python3 $(KERNEL_ROOT)/scripts/ir0_selftest_run.py \
		--arch arm64 \
		--binary $(IR0_SELFTEST_ARM64_SCHED_LAT) \
		--done "ok 1 sched_lat" \
		--log $(HOST_TMP)/ir0-selftest-arm64-sched-lat.log
	@echo "✓ ir0-selftest-sched-lat-arm64"

ir0-selftest-sched-lat: ir0-selftest-sched-lat-x86 ir0-selftest-sched-lat-arm64
