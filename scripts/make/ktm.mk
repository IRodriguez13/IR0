# SPDX-License-Identifier: GPL-3.0-only
#
# KTM, kernel tests and host-side gates.
#
# Everything here uses the kernel and the two harness trees; nothing reaches
# into ISD, the QA smoke catalog or a product image.

.PHONY: ctr test-fast agent-fast ktest kernel-tests tests kernel-memsafe \
	kernel-analyze kernel-text-budget health ktm-check ktm-manifest \
	ktm-classify-selftest ktm-adoption-guard host-portability-guard \
	build-matrix-min config-wiring-check arch-config-check \
	repo-hygiene-guard truth-tests test test-list test-run

# CTR is the fast merge gate: kernel + architecture boundary + config matrix +
# host unit tests. `scripts/ctr.sh` owns the ordering so agents and humans run
# exactly the same sequence.
ctr:
	@chmod +x scripts/ctr.sh
	@./scripts/ctr.sh

test-fast: kernel-x64.bin arch-guard
	@$(MAKE) -s -C tests/host run
	@$(MAKE) -s ktm-check

agent-fast: test-fast kernel-tests
	@echo "✓ agent-fast OK (host + ktest)"

# KTM is the kernel's in-tree tracing/assertion layer. These two gates are
# cheap and catch manifest drift without booting QEMU.
ktm-check: kernel-x64.bin arch-guard
	@$(MAKE) -s -C tests/host run
	@python3 scripts/ktm_classify_selftest.py
	@python3 scripts/ktm_panic_inventory.py --check
	@python3 scripts/ktm_syscall_manifest.py --tier1 || true
	@$(MAKE) -s ktm-adoption-guard
	@$(MAKE) -s host-portability-guard
	@echo "✓ KTM check complete (tier-1 manifest gaps are informational)"

ktm-classify-selftest:
	@python3 scripts/ktm_classify_selftest.py

ktm-manifest:
	@python3 scripts/ktm_syscall_manifest.py --tier1

ktm-adoption-guard:
	@python3 $(KERNEL_ROOT)/scripts/ktm_adoption_guard.py

host-portability-guard:
	@python3 $(KERNEL_ROOT)/scripts/host_portability_guard.py

# Host harnesses used by CTR and CI.
tests: kernel-x64-test.bin
	@echo "  TEST    Building kernel-memsafe (kernel code under Valgrind)..."
	@$(MAKE) -C tests/kernel_memsafe KERNEL_ROOT=$(KERNEL_ROOT) all
	@echo "  TEST    Running host test suite..."
	@$(MAKE) -C tests/host run
	@echo "✓ Tests built: tests/kernel_memsafe/ir0_kernel_memsafe, kernel-x64-test.bin"

kernel-memsafe:
	@echo "  KERNEL-MEMSAFE  Building kernel code for host and running Valgrind..."
	@$(MAKE) -C tests/kernel_memsafe KERNEL_ROOT=$(KERNEL_ROOT) memsafe
	@echo "✓ kernel-memsafe passed"

# KUnit-style battery that runs at boot. QEMU is headless and disk-only; the
# private log is copied to KTEST_LOG for evidence after the run.
KTEST_LOG ?= $(HOST_TMP)/ktest.log

ktest: kernel-tests

kernel-tests: kernel-x64-test.iso disk.img
	@echo "  KTEST   Running in-kernel test suite at boot (KUnit-style)..."
	@ktest_log=$$(mktemp "$${TMPDIR:-/tmp}/ir0-ktest.XXXXXX") || exit 1; \
	$(SMOKE_QEMU_RUN) --log "$$ktest_log" --timeout 60 --expected-panic \
		--done "TEST_END|kernel.ktest|PASS" \
		--fail-regex 'Some tests FAILED|not ok |TEST_END[|]kernel[.]ktest[|]FAIL' -- \
		$(QEMU_64_CMD) -cdrom kernel-x64-test.iso -drive file=disk.img,format=raw,if=ide,index=0 -serial stdio -display none -m 128M -no-reboot -net none; \
	run_status=$$?; \
	passed=0; \
	if [ $$run_status -eq 0 ] && \
	   grep -q "All .* test(s) passed" "$$ktest_log" && \
	   grep -q 'TEST_END|kernel.ktest|PASS' "$$ktest_log" && \
	   ! grep -q "Some tests FAILED" "$$ktest_log" && \
	   ! grep -q "not ok " "$$ktest_log" && \
	   ! grep -q "# SKIP need process" "$$ktest_log"; then \
		passed=1; \
	fi; \
	if ! cp -f "$$ktest_log" "$(KTEST_LOG)"; then \
		rm -f "$$ktest_log" "$$ktest_log.qemu-stderr"; \
		echo "✗ kernel-tests FAILED (cannot write $(KTEST_LOG))"; \
		exit 1; \
	fi; \
	if [ -f "$$ktest_log.qemu-stderr" ]; then \
		cp -f "$$ktest_log.qemu-stderr" "$(KTEST_LOG).qemu-stderr" 2>/dev/null || true; \
	fi; \
	rm -f "$$ktest_log" "$$ktest_log.qemu-stderr"; \
	if [ $$passed -eq 1 ]; then echo "✓ kernel-tests passed"; exit 0; else echo "✗ kernel-tests FAILED"; exit 1; fi

# Kernel binary analysis: sections (size -A), undefined symbols, kmain.
kernel-analyze: kernel-x64.bin
	@echo "  ANALYZE kernel-x64.bin"
	@echo "----------------------------------------"
	@echo "Section sizes (size -A):"
	@size -A kernel-x64.bin
	@echo "----------------------------------------"
	@echo "Undefined symbols (nm --undefined-only, first 20):"
	@nm --undefined-only kernel-x64.bin 2>/dev/null | head -20 || true
	@UNDEF=$$(nm --undefined-only kernel-x64.bin 2>/dev/null | wc -l); echo "Undefined symbol count: $$UNDEF"
	@echo "----------------------------------------"
	@echo "Entry symbol:"
	@nm kernel-x64.bin 2>/dev/null | grep -E ' [Tt] kmain$$' || true
	@if nm kernel-x64.bin 2>/dev/null | grep -q ' [Tt] kmain$$'; then echo "✓ kernel-analyze passed (kmain present)"; else echo "✗ kernel-analyze FAILED (kmain not found)"; exit 1; fi

# H6: kernel .text regression gate (raise KERNEL_TEXT_BUDGET only with documented reason).
# -O2 baseline is ~817 KiB; keep ~5% headroom for small fixes, not new subsystems.
KERNEL_TEXT_BUDGET ?= 860000

kernel-text-budget: kernel-x64.bin
	@TEXT=$$(size -A kernel-x64.bin | awk '/^\.text/{print $$2}'); \
	echo "kernel .text = $$TEXT bytes (budget $(KERNEL_TEXT_BUDGET))"; \
	if [ -z "$$TEXT" ] || [ "$$TEXT" -gt "$(KERNEL_TEXT_BUDGET)" ]; then \
		echo "✗ kernel-text-budget FAILED"; exit 1; \
	fi; \
	echo "✓ kernel-text-budget passed"

health: kernel-analyze kernel-text-budget
	@echo ""
	@echo "  HEALTH  Running full test suite..."
	@$(MAKE) kernel-memsafe && $(MAKE) kernel-tests
	@echo ""
	@echo "✓ health passed (kernel-analyze, kernel-text-budget, kernel-memsafe, kernel-tests)"

# Minimal permanent build matrix for modular configs. The matrix is x86_64:
# force ARCH so a caller's `ARCH=arm64` cannot poison the generated objects.
# The caller's .config is restored on every exit path, including failures.
build-matrix-min:
	@$(MAKE) -s config-wiring-check
	@$(MAKE) -s arch-config-check
	@set -eu; \
	bak=$$(mktemp "$${TMPDIR:-/tmp}/ir0-config.XXXXXX"); \
	had=0; \
	if [ -f .config ]; then cp .config "$$bak"; had=1; fi; \
	restore() { \
		rc=$$?; \
		trap - EXIT INT TERM; \
		if [ $$had -eq 1 ]; then \
			cp "$$bak" .config; \
			python3 scripts/kconfig/menuconfig.py --sync >/dev/null; \
		else \
			rm -f .config; \
		fi; \
		rm -f "$$bak"; \
		exit $$rc; \
	}; \
	trap restore EXIT INT TERM; \
	echo "  MATRIX  defconfig"; \
	$(MAKE) defconfig >/dev/null; \
	$(MAKE) -s ARCH=x86_64 kernel-x64.bin >/dev/null; \
	echo "  MATRIX  tiny preset"; \
	python3 scripts/kconfig/menuconfig.py --preset tiny >/dev/null; \
	$(MAKE) -s ARCH=x86_64 kernel-x64.bin >/dev/null; \
	echo "  MATRIX  networking disabled"; \
	$(MAKE) defconfig >/dev/null; \
	python3 scripts/kconfig/menuconfig.py --set ENABLE_NETWORKING=n INIT_NETWORK_STACK=n >/dev/null; \
	$(MAKE) -s ARCH=x86_64 kernel-x64.bin >/dev/null; \
	echo "  MATRIX  storage fully disabled"; \
	$(MAKE) defconfig >/dev/null; \
	python3 scripts/kconfig/menuconfig.py --set ENABLE_STORAGE_ATA=n ENABLE_STORAGE_ATA_BLOCK=n INIT_STORAGE_ATA=n INIT_STORAGE_ATA_BLOCK=n >/dev/null; \
	$(MAKE) -s ARCH=x86_64 kernel-x64.bin >/dev/null; \
	echo "  MATRIX  storage core without block layer"; \
	$(MAKE) defconfig >/dev/null; \
	python3 scripts/kconfig/menuconfig.py --set ENABLE_STORAGE_ATA=y ENABLE_STORAGE_ATA_BLOCK=n INIT_STORAGE_ATA=y INIT_STORAGE_ATA_BLOCK=n >/dev/null; \
	$(MAKE) -s ARCH=x86_64 kernel-x64.bin >/dev/null; \
	echo "  MATRIX  tmpfs-only root"; \
	$(MAKE) defconfig >/dev/null; \
	python3 scripts/kconfig/menuconfig.py --set ENABLE_FS_MINIX=n ENABLE_FS_TMPFS=y ROOT_FILESYSTEM=tmpfs >/dev/null; \
	$(MAKE) -s ARCH=x86_64 kernel-x64.bin >/dev/null; \
	echo "  MATRIX  scheduler policy 1"; \
	$(MAKE) defconfig >/dev/null; \
	python3 scripts/kconfig/menuconfig.py --set SCHEDULER_POLICY=1 >/dev/null; \
	$(MAKE) -s ARCH=x86_64 kernel-x64.bin >/dev/null; \
	echo "  MATRIX  scheduler policy 2"; \
	$(MAKE) defconfig >/dev/null; \
	python3 scripts/kconfig/menuconfig.py --set SCHEDULER_POLICY=2 >/dev/null; \
	$(MAKE) -s ARCH=x86_64 kernel-x64.bin >/dev/null; \
	echo "  MATRIX  arm64 config scaffold"; \
	$(MAKE) defconfig >/dev/null; \
	python3 scripts/kconfig/menuconfig.py --set ARCH_X86_64=n ARCH_ARM64=y DRV_NIC_RTL8139=n DRV_NIC_E1000=n >/dev/null; \
	$(MAKE) -s arch-config-check >/dev/null; \
	echo "  MATRIX  USB host enabled"; \
	$(MAKE) defconfig >/dev/null; \
	python3 scripts/kconfig/menuconfig.py --set ENABLE_USB_HOST=y INIT_USB_HOST=y >/dev/null; \
	$(MAKE) -s ARCH=x86_64 kernel-x64.bin >/dev/null; \
	echo "  MATRIX  Bluetooth disabled"; \
	$(MAKE) defconfig >/dev/null; \
	python3 scripts/kconfig/menuconfig.py --set ENABLE_BLUETOOTH=n INIT_BLUETOOTH_DRIVER=n >/dev/null; \
	$(MAKE) -s ARCH=x86_64 kernel-x64.bin >/dev/null; \
	echo "  MATRIX  lazy MM disabled (eager mmap/brk bisect)"; \
	$(MAKE) defconfig >/dev/null; \
	python3 scripts/kconfig/menuconfig.py --set LAZY_ANON_MMAP=n LAZY_BRK_HEAP=n >/dev/null; \
	$(MAKE) -s ARCH=x86_64 kernel-x64.bin >/dev/null; \
	$(MAKE) defconfig >/dev/null; \
	$(MAKE) -s arch-guard; \
	echo "✓ build-matrix-min passed"

config-wiring-check:
	@if [ -f "$(KERNEL_ROOT)/.config" ] && [ ! -f "$(KERNEL_ROOT)/includes/generated/autoconf.h" ]; then \
		echo "✗ config-wiring-check: includes/generated/autoconf.h missing"; \
		exit 1; \
	fi
	@echo "✓ config-wiring-check passed"

arch-config-check:
	@if [ "$(CONFIG_ARCH_X86_64)" = "y" ] && [ "$(CONFIG_ARCH_ARM64)" = "y" ]; then \
		echo "✗ arch-config-check: both CONFIG_ARCH_X86_64 and CONFIG_ARCH_ARM64 are enabled"; \
		exit 1; \
	fi
	@if [ "$(CONFIG_ARCH_X86_64)" != "y" ] && [ "$(CONFIG_ARCH_ARM64)" != "y" ]; then \
		echo "✗ arch-config-check: no architecture selected"; \
		exit 1; \
	fi
	@echo "✓ arch-config-check passed"

repo-hygiene-guard:
	@python3 $(KERNEL_ROOT)/scripts/repo_hygiene_guard.py

truth-tests:
	@python3 scripts/test_truth_tooling.py

# Small test catalog / TUI. The catalog only lists targets that still exist.
test:
	@python3 scripts/test_tui.py

test-list:
	@python3 scripts/test_runner.py --list

test-run:
	@test -n "$(SUITE)" || (echo "usage: make test-run SUITE=host,ktest|ctr" >&2; exit 2)
	@python3 scripts/test_runner.py --suites "$(SUITE)" $(if $(filter 1,$(IR0_DEV_PERSIST)),--persist,)
