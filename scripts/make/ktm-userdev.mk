# SPDX-License-Identifier: GPL-3.0-only
#
# Guest MM/fork pilots on the x86-64 rescue ISO.
#
# Every run goes through scripts/ktm_userdev_runner.py, which owns the work
# disk (a MINIX copy of disk.img), the virtio-9p share, the QEMU memory cap
# and the autokill timeout (scripts/smoke_qemu_run.sh). The caller's disk.img
# is never modified. Logs land in $(HOST_TMP).
#
#   ktm-userdev-cow-run         fork COW page and MAP_FIXED split
#   ktm-userdev-fork-storm-run  fork/exit/wait storm, no process or frame leak
#   smoke-mm-cow-lazy           lazy brk, lazy anon mmap, fork COW A-D
#
# The KTM cases run as the payload of the hostshare stub, so /dev/ktm
# (CONFIG_KTM_USERDEV) and virtio-9p must be in the kernel .config; the MM
# smokes run directly as /sbin/init.

.PHONY: ktm-userdev-cow-run ktm-userdev-fork-storm-run \
	smoke-mm-cow-lazy

KTM_USERDEV_MUSL_CC ?= $(shell command -v musl-gcc 2>/dev/null || \
	command -v x86_64-linux-musl-gcc 2>/dev/null)
KTM_USERDEV_OUT := $(KERNEL_ROOT)/build/ktm-userdev
KTM_USERDEV_LIB := $(KERNEL_ROOT)/tests/ktm/lib
# -idirafter: musl's <stdint.h> wins, <ir0/...> uapi still resolves.
KTM_USERDEV_CFLAGS := -static -Os -I$(KTM_USERDEV_LIB) \
	-idirafter $(KERNEL_ROOT)/includes
KTM_USERDEV_STUB := $(KTM_USERDEV_OUT)/init_hostshare_exec
KTM_USERDEV_RUNNER := python3 $(KERNEL_ROOT)/scripts/ktm_userdev_runner.py \
	--stub $(KTM_USERDEV_STUB)

define ktm_userdev_need_musl
	@if [ -z "$(KTM_USERDEV_MUSL_CC)" ]; then \
		echo "✗ musl x86-64 compiler not found: install musl-tools" \
			"(or set KTM_USERDEV_MUSL_CC=...)" >&2; \
		exit 1; \
	fi
	@mkdir -p $(KTM_USERDEV_OUT)
endef

$(KTM_USERDEV_STUB): setup/pid1/init_hostshare_exec.c
	$(ktm_userdev_need_musl)
	@echo "  CC      $@"
	@$(KTM_USERDEV_MUSL_CC) -static -Os -o $@ $<

$(KTM_USERDEV_OUT)/ktm_%_case: tests/ktm/userdev/ktm_%_case.c \
		$(KTM_USERDEV_LIB)/libktm_user.c $(KTM_USERDEV_LIB)/libktm_user.h
	$(ktm_userdev_need_musl)
	@echo "  CC      $@"
	@$(KTM_USERDEV_MUSL_CC) $(KTM_USERDEV_CFLAGS) -o $@ $< \
		$(KTM_USERDEV_LIB)/libktm_user.c

$(KTM_USERDEV_OUT)/init_%_smoke: setup/pid1/init_%_smoke.c
	$(ktm_userdev_need_musl)
	@echo "  CC      $@"
	@$(KTM_USERDEV_MUSL_CC) -static -Os -o $@ $<

KTM_USERDEV_DEPS := kernel-x64-userspace.iso disk.img $(KTM_USERDEV_STUB)

ktm-userdev-cow-run: $(KTM_USERDEV_DEPS) $(KTM_USERDEV_OUT)/ktm_cow_touch_case
	@$(KTM_USERDEV_RUNNER) --init $(KTM_USERDEV_OUT)/ktm_cow_touch_case \
		--log $(HOST_TMP)/ktm-userdev-cow-run.log --timeout 90 \
		--done KTM_USERDEV_COW_OK \
		--require 'TEST_END|cow_touch|PASS'

ktm-userdev-fork-storm-run: $(KTM_USERDEV_DEPS) \
		$(KTM_USERDEV_OUT)/ktm_fork_storm_case
	@$(KTM_USERDEV_RUNNER) --init $(KTM_USERDEV_OUT)/ktm_fork_storm_case \
		--log $(HOST_TMP)/ktm-userdev-fork-storm-run.log --timeout 240 \
		--done KTM_USERDEV_FORK_STORM_OK \
		--require 'TEST_END|fork_exit_storm|PASS'

# Three boots, each with the smoke as /sbin/init on a fresh work disk. The
# fork phase reads etc/f40.dat to check that a file-backed read survives fork.
smoke-mm-cow-lazy: kernel-x64-userspace.iso disk.img \
		$(KTM_USERDEV_OUT)/init_heap_smoke \
		$(KTM_USERDEV_OUT)/init_mmap_smoke \
		$(KTM_USERDEV_OUT)/init_fork_mem_smoke
	@echo "  SMOKE   MM lazy brk + lazy mmap + fork COW"
	@$(KTM_USERDEV_RUNNER) --legacy-disk-init --no-fsdev \
		--init $(KTM_USERDEV_OUT)/init_heap_smoke \
		--log $(HOST_TMP)/smoke-mm-cow-lazy-heap.log --timeout 90 \
		--done 'page_present=1' --require 'HEAP_SMOKE heap_start='
	@$(KTM_USERDEV_RUNNER) --legacy-disk-init --no-fsdev \
		--init $(KTM_USERDEV_OUT)/init_mmap_smoke \
		--log $(HOST_TMP)/smoke-mm-cow-lazy-mmap.log --timeout 90 \
		--done '[PF] userspace segv pid=' \
		--require 'MMAP_SMOKE mapped=1' --require 'verify=1' \
		--require 'expected_segv=1'
	@dat=$$(mktemp "$(HOST_TMP)/ir0-mm-cow.XXXXXX") || exit 1; \
	printf 'PARENT-FILE-OK!' > "$$dat"; \
	$(KTM_USERDEV_RUNNER) --legacy-disk-init --no-fsdev \
		--init $(KTM_USERDEV_OUT)/init_fork_mem_smoke \
		--inject "$$dat:etc/f40.dat" \
		--log $(HOST_TMP)/smoke-mm-cow-lazy-fork.log --timeout 180 \
		--done 'MM_COW_SUMMARY A=0 B=0 C=0 D=0' \
		--require 'MM_COW_D ' --require 'leaks=0'; \
	rc=$$?; rm -f "$$dat"; exit $$rc
	@echo "✓ smoke-mm-cow-lazy passed (lazy brk + lazy mmap + fork COW A-D)"
