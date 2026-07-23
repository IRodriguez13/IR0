# SPDX-License-Identifier: GPL-3.0-only
# Historical QEMU smokes (FASE/userspace bring-up). Not part of default CTR.
#
# Enable when debugging a specific phase:
#   make IR0_LEGACY_SMOKE=1 smoke-fase50-busybox
#   make IR0_LEGACY_SMOKE=1 smoke-regression-full
#
# C sources under setup/pid1/ and scripts/ remain; only Makefile wiring lives here.
# QEMU smoke: ring-3 /sbin/init must print init smoke marker on serial.
# Requires: build-init-smoke, disk.img with /sbin/init (sudo make load-init).
smoke-userspace-init: build-init-smoke load-init-with-smoke kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) create-disk; \
	fi
	@if [ ! -f $(INIT_SMOKE_BIN) ]; then \
		echo "✗ $(INIT_SMOKE_BIN) missing — run make build-init-smoke"; \
		exit 1; \
	fi
	@echo "  SMOKE   userspace /sbin/init boot (CONFIG_KERNEL_DEBUG_SHELL=n)..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	$(SMOKE_QEMU_RUN) --log /tmp/userspace-smoke.log --timeout 90 --done init smoke (ring-3) ok -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 128M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "init smoke (ring-3) ok" /tmp/userspace-smoke.log; then \
		echo "✓ smoke-userspace-init passed"; \
	elif grep -q "Failed to read file from filesystem" /tmp/userspace-smoke.log; then \
		echo "✗ /sbin/init not on disk.img — run: make load-init-with-smoke"; \
		exit 1; \
	else \
		echo "✗ smoke-userspace-init FAILED"; \
		exit 1; \
	fi

load-init-with-smoke: build-init-smoke
	@./scripts/load_init.sh --inject

load-init-with-musl: build-init-musl
	@./scripts/load_init.sh --inject disk.img $(INIT_SMOKE_BIN)

# QEMU smoke: musl CRT + TLS + syscall insn ABI.
smoke-userspace-musl: build-init-musl load-init-with-musl kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) create-disk; \
	fi
	@if [ ! -f $(INIT_SMOKE_BIN) ]; then \
		echo "✗ $(INIT_SMOKE_BIN) missing — run make build-init-musl"; \
		exit 1; \
	fi
	@echo "  SMOKE   musl /sbin/init boot (CONFIG_KERNEL_DEBUG_SHELL=n)..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	$(SMOKE_QEMU_RUN) --log /tmp/userspace-musl-smoke.log --timeout 90 \
		--done "IR0: musl init smoke ok" -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 128M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "IR0: musl init smoke ok" /tmp/userspace-musl-smoke.log; then \
		echo "✓ smoke-userspace-musl passed"; \
	elif grep -q "Failed to read file from filesystem" /tmp/userspace-musl-smoke.log; then \
		echo "✗ /sbin/init not on disk.img — run: make load-init-with-musl"; \
		exit 1; \
	else \
		echo "✗ smoke-userspace-musl FAILED"; \
		exit 1; \
	fi

# QEMU smoke: musl arch_prctl(ARCH_SET_FS) — mandatory gate before F (LSTAR/sysret).
smoke-musl-arch-prctl: build-musl-arch-prctl-smoke kernel-x64-userspace.iso
	@echo "  SMOKE   musl arch_prctl minimal (ARCH_SET_FS + write ok + exit_group)..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=64 status=none && \
	python3 scripts/inject_init_minix.py --format $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(MUSL_ARCH_PRCTL_BIN) sbin/init && \
	python3 scripts/verify_minix_rootfs.py $$DISK /sbin/init && \
	$(SMOKE_QEMU_RUN) --log $(MUSL_ARCH_PRCTL_LOG) --profile musl-arch-prctl \
		--done MUSL_ARCH_PRCTL_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 128M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "MUSL_ARCH_PRCTL_OK" $(MUSL_ARCH_PRCTL_LOG) && \
	    grep -qE '(^|[^a-z])ok' $(MUSL_ARCH_PRCTL_LOG); then \
		echo "✓ smoke-musl-arch-prctl finished"; \
	else \
		echo "✗ smoke-musl-arch-prctl FAILED"; \
		if grep -qE '#PF|General protection|panic' $(MUSL_ARCH_PRCTL_LOG); then \
			echo "--- arch_prctl fault hints ---"; \
			grep -E '#PF|General protection|panic|arch_prctl' $(MUSL_ARCH_PRCTL_LOG) | tail -5; \
		fi; \
		exit 1; \
	fi

# Inject minimal PID1 + /bin/sh stub on MINIX disk (no mount).
load-userspace-rootfs: build-init-minimal build-sh-smoke
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) create-disk; \
	fi
	@chmod +x scripts/load_userspace_rootfs.sh
	@./scripts/load_userspace_rootfs.sh disk.img $(INIT_SMOKE_BIN) $(SH_SMOKE_BIN)

# Inject irinit + BusyBox on MINIX disk (transitional PID1 before runit).
load-userspace-irinit: build-irinit build-busybox-fase50-min
	@DISK=$${DISK:-disk.img}; \
	echo "  DISK    Preparing $$DISK (200M MINIX)..."; \
	dd if=/dev/zero of=$$DISK bs=1M count=200 status=none; \
	python3 scripts/inject_init_minix.py --format-large $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(IRINIT_BIN) sbin/init && \
	python3 scripts/inject_init_minix.py $$DISK $(FASE50_BUSYBOX_BIN) bin/busybox && \
	python3 scripts/inject_init_minix.py $$DISK $(FASE50_BUSYBOX_BIN) bin/sh && \
	python3 scripts/verify_minix_rootfs.py $$DISK /sbin/init /bin/sh /bin/busybox
	@echo "✓ load-userspace-irinit OK (irinit → /bin/sh; replace with runit later)"

smoke-userspace-irinit: load-userspace-irinit kernel-x64-userspace.iso
	@echo "  SMOKE   irinit PID1 boot (transitional, ring 3)..."
	@DISK=$$(mktemp /tmp/ir0-irinit-smoke.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	$(SMOKE_QEMU_RUN) --log /tmp/irinit-boot-smoke.log --timeout 120 --done IRINIT_PID1_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK
	@if grep -q "IRINIT_PID1_OK" /tmp/irinit-boot-smoke.log && \
	    grep -q "DEV_CONSOLE_OPEN_OK" /tmp/irinit-boot-smoke.log; then \
		echo "✓ smoke-userspace-irinit passed"; \
	else \
		echo "✗ smoke-userspace-irinit FAILED"; \
		grep -E 'IRINIT_|KERNEL PANIC|panic' /tmp/irinit-boot-smoke.log | tail -20; \
		exit 1; \
	fi

# QEMU smoke: PID1 fork+execve /bin/sh in ring 3.
smoke-userspace-shell: load-userspace-rootfs kernel-x64-userspace.iso
	@echo "  SMOKE   userspace fork/exec /bin/sh (init_minimal PID1)..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	$(SMOKE_QEMU_RUN) --log /tmp/userspace-shell-smoke.log --timeout 90 --done shell smoke ok -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 128M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "shell smoke ok" /tmp/userspace-shell-smoke.log; then \
		echo "✓ smoke-userspace-shell passed"; \
	elif grep -q "/bin/sh missing" /tmp/userspace-shell-smoke.log; then \
		echo "✗ /bin/sh not on disk — run: make load-userspace-rootfs"; \
		exit 1; \
	else \
		echo "✗ smoke-userspace-shell FAILED"; \
		exit 1; \
	fi

smoke-userspace-segv: build-init-segv-smoke build-userspace-segv kernel-x64-userspace.iso
	@echo "  SMOKE   userspace #PF -> SIGSEGV (PID1 wait4)..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	python3 scripts/inject_init_minix.py $$DISK $(SEGV_SMOKE_BIN) bin/userspace_segv; \
	$(SMOKE_QEMU_RUN) --log /tmp/userspace-segv-smoke.log --timeout 90 --done code=000000000000008B -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 128M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "\\[PF\\] userspace segv pid=" /tmp/userspace-segv-smoke.log && \
	    grep -q "\\[PROCESS\\] exit pid=.*code=000000000000008B" /tmp/userspace-segv-smoke.log; then \
		echo "✓ smoke-userspace-segv passed"; \
	else \
		echo "✗ smoke-userspace-segv FAILED"; \
		exit 1; \
	fi

smoke-userspace-heap: build-init-heap-smoke kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) create-disk; \
	fi
	@echo "  SMOKE   userspace brk/sbrk heap smoke..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	$(SMOKE_QEMU_RUN) --log $(HEAP_SMOKE_LOG) --timeout 90 --done page_present=1 -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 128M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE39_HEAP" $(HEAP_SMOKE_LOG) && \
	    grep -q "page_present=1" $(HEAP_SMOKE_LOG); then \
		echo "✓ smoke-userspace-heap passed"; \
	else \
		echo "✗ smoke-userspace-heap FAILED"; \
		exit 1; \
	fi

smoke-userspace-mmap: build-init-mmap-smoke kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) create-disk; \
	fi
	@echo "  SMOKE   userspace mmap/munmap smoke..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	$(SMOKE_QEMU_RUN) --log $(MMAP_SMOKE_LOG) --timeout 90 --done FASE39_MMAP mapped=1 -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 128M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE39_MMAP mapped=1" $(MMAP_SMOKE_LOG) && \
	    grep -q "\\[PF\\] userspace segv pid=" $(MMAP_SMOKE_LOG); then \
		echo "✓ smoke-userspace-mmap passed"; \
	else \
		echo "✗ smoke-userspace-mmap FAILED"; \
		exit 1; \
	fi

smoke-userspace-stack-heap-iso: build-init-stack-heap-iso-smoke kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) create-disk; \
	fi
	@echo "  SMOKE   userspace stack/heap isolation smoke..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	$(SMOKE_QEMU_RUN) --log $(ISO_SMOKE_LOG) --timeout 90 --done overlap=0 -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 128M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE39_ISO" $(ISO_SMOKE_LOG) && \
	    grep -q "overlap=0" $(ISO_SMOKE_LOG); then \
		echo "✓ smoke-userspace-stack-heap-iso passed"; \
	else \
		echo "✗ smoke-userspace-stack-heap-iso FAILED"; \
		exit 1; \
	fi

smoke-userspace-fork-mem: build-init-fork-mem-smoke kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) create-disk; \
	fi
	@echo "  SMOKE   userspace fork memory audit (A/B/C/D)..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	$(SMOKE_QEMU_RUN) --log $(FORK_MEM_SMOKE_LOG) --timeout 120 --done FASE40_SUMMARY -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE40_SUMMARY" $(FORK_MEM_SMOKE_LOG); then \
		echo "✓ smoke-userspace-fork-mem finished"; \
	else \
		echo "✗ smoke-userspace-fork-mem FAILED"; \
		exit 1; \
	fi

smoke-userspace-fase41-reclaim: build-init-fase41-reclaim build-fase41-true kernel-x64-userspace.iso
	@echo "  WARN    DEPRECATED: prefer 'make ktm-run' + 'make ktm-userdev-fork-storm-run' (see Documentation/KTM_FASE_INVENTORY.md)"
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) create-disk; \
	fi
	@echo "  SMOKE   FASE41 reclaim + ownership audit..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	python3 scripts/inject_init_minix.py $$DISK $(FASE41_TRUE_BIN) bin/f41true; \
	$(SMOKE_QEMU_RUN) --log $(FASE41_RECLAIM_LOG) --timeout 120 --done FASE41_SUMMARY -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE41_SUMMARY" $(FASE41_RECLAIM_LOG); then \
		echo "✓ smoke-userspace-fase41-reclaim finished"; \
	else \
		echo "✗ smoke-userspace-fase41-reclaim FAILED"; \
		exit 1; \
	fi

smoke-page-table-reclaim: build-init-fase42-pt-reclaim build-fase41-true kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) create-disk; \
	fi
	@echo "  SMOKE   FASE42 page-table reclaim..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	python3 scripts/inject_init_minix.py $$DISK $(FASE41_TRUE_BIN) bin/f41true; \
	$(SMOKE_QEMU_RUN) --log $(FASE42_PT_RECLAIM_LOG) --timeout 180 --done FASE42_PT_RECLAIM -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE42_PT_RECLAIM" $(FASE42_PT_RECLAIM_LOG); then \
		echo "✓ smoke-page-table-reclaim finished"; \
	else \
		echo "✗ smoke-page-table-reclaim FAILED"; \
		exit 1; \
	fi

smoke-exec-storm: build-init-fase42-exec-storm build-fase41-true kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) create-disk; \
	fi
	@echo "  SMOKE   FASE42 exec storm..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	python3 scripts/inject_init_minix.py $$DISK $(FASE41_TRUE_BIN) bin/f41true; \
	$(SMOKE_QEMU_RUN) --log $(FASE42_EXEC_STORM_LOG) --timeout 200 --done FASE42_EXEC_STORM -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE42_EXEC_STORM" $(FASE42_EXEC_STORM_LOG); then \
		echo "✓ smoke-exec-storm finished"; \
	else \
		echo "✗ smoke-exec-storm FAILED"; \
		exit 1; \
	fi

smoke-fork-exit-storm: build-init-fase42-fork-exit-storm kernel-x64-userspace.iso
	@echo "  WARN    DEPRECATED: prefer 'make ktm-userdev-fork-storm-run' (see Documentation/KTM_FASE_INVENTORY.md)"
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) create-disk; \
	fi
	@echo "  SMOKE   FASE42 fork+exit storm..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	$(SMOKE_QEMU_RUN) --log $(FASE42_FORK_EXIT_STORM_LOG) --timeout 150 --done FASE42_FORK_EXIT_STORM -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE42_FORK_EXIT_STORM" $(FASE42_FORK_EXIT_STORM_LOG); then \
		echo "✓ smoke-fork-exit-storm finished"; \
	else \
		echo "✗ smoke-fork-exit-storm FAILED"; \
		exit 1; \
	fi

smoke-fase43-fork-exit-storm: build-init-fase43-fork-exit-storm kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) create-disk; \
	fi
	@echo "  SMOKE   FASE43 fork+exit storm (256)..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	$(SMOKE_QEMU_RUN) --log $(FASE43_FORK_EXIT_STORM_LOG) --timeout 180 --done FASE43_FORK_EXIT_STORM -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE43_FORK_EXIT_STORM" $(FASE43_FORK_EXIT_STORM_LOG); then \
		echo "✓ smoke-fase43-fork-exit-storm finished"; \
	else \
		echo "✗ smoke-fase43-fork-exit-storm FAILED"; \
		exit 1; \
	fi

smoke-fase43-fork-wait-storm: build-init-fase43-fork-wait-storm kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) create-disk; \
	fi
	@echo "  SMOKE   FASE43 fork+wait storm..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	$(SMOKE_QEMU_RUN) --log $(FASE43_FORK_WAIT_STORM_LOG) --timeout 180 --done FASE43_FORK_WAIT_STORM -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE43_FORK_WAIT_STORM" $(FASE43_FORK_WAIT_STORM_LOG); then \
		echo "✓ smoke-fase43-fork-wait-storm finished"; \
	else \
		echo "✗ smoke-fase43-fork-wait-storm FAILED"; \
		exit 1; \
	fi

smoke-fase43-exec-loop: build-init-fase43-exec-loop build-fase41-true kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) create-disk; \
	fi
	@echo "  SMOKE   FASE43 exec loop (1024)..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	python3 scripts/inject_init_minix.py $$DISK $(FASE41_TRUE_BIN) bin/f41true; \
	$(SMOKE_QEMU_RUN) --log $(FASE43_EXEC_LOOP_LOG) --timeout 240 --done FASE43_EXEC_LOOP -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE43_EXEC_LOOP" $(FASE43_EXEC_LOOP_LOG); then \
		echo "✓ smoke-fase43-exec-loop finished"; \
	else \
		echo "✗ smoke-fase43-exec-loop FAILED"; \
		exit 1; \
	fi

smoke-fase44-fork-wait-drain: build-init-fase44-fork-wait-drain kernel-x64-userspace.iso
	@echo "  WARN    DEPRECATED: prefer 'make ktm-userdev-fork-storm-run' (256 drain + KTM asserts; see KTM_FASE_INVENTORY.md)"
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) create-disk; \
	fi
	@echo "  SMOKE   FASE44 fork-wait-drain (512)..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	$(SMOKE_QEMU_RUN) --log $(FASE44_FORK_WAIT_DRAIN_LOG) --timeout 180 --done FASE44_FORK_WAIT_DRAIN -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE44_FORK_WAIT_DRAIN" $(FASE44_FORK_WAIT_DRAIN_LOG); then \
		echo "✓ smoke-fase44-fork-wait-drain finished"; \
	else \
		echo "✗ smoke-fase44-fork-wait-drain FAILED"; \
		exit 1; \
	fi

smoke-fase44-exec-drain: build-init-fase44-exec-drain build-fase41-true kernel-x64-userspace.iso
	@echo "  WARN    DEPRECATED: prefer 'make ktm-userdev-exec-drain-virtfs-run' (see Documentation/KTM_FASE_INVENTORY.md)"
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) create-disk; \
	fi
	@echo "  SMOKE   FASE44 exec-drain (1024)..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	python3 scripts/inject_init_minix.py $$DISK $(FASE41_TRUE_BIN) bin/f41true; \
	$(SMOKE_QEMU_RUN) --log $(FASE44_EXEC_DRAIN_LOG) --timeout 300 --done FASE44_EXEC_DRAIN -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE44_EXEC_DRAIN" $(FASE44_EXEC_DRAIN_LOG); then \
		echo "✓ smoke-fase44-exec-drain finished"; \
	else \
		echo "✗ smoke-fase44-exec-drain FAILED"; \
		exit 1; \
	fi

smoke-fase44-init-exit-drain: build-init-fase44-init-exit-drain kernel-x64-userspace.iso
	@echo "  WARN    DEPRECATED: prefer 'make ktm-userdev-init-exit-drain-virtfs-run'"
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) create-disk; \
	fi
	@echo "  SMOKE   FASE44 init-exit-drain..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	$(SMOKE_QEMU_RUN) --log $(FASE44_INIT_EXIT_DRAIN_LOG) --timeout 180 --done FASE44_INIT_EXIT_DRAIN -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE44_INIT_EXIT_DRAIN" $(FASE44_INIT_EXIT_DRAIN_LOG); then \
		echo "✓ smoke-fase44-init-exit-drain finished"; \
	else \
		echo "✗ smoke-fase44-init-exit-drain FAILED"; \
		exit 1; \
	fi

smoke-fase45-fork-rollback-storm: build-init-fase45-fork-rollback-storm kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) create-disk; \
	fi
	@echo "  SMOKE   FASE45 fork rollback storm (2048)..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	$(SMOKE_QEMU_RUN) --log $(FASE45_FORK_ROLLBACK_STORM_LOG) --timeout 180 --done FASE45_FORK_ROLLBACK_STORM -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE45_FORK_ROLLBACK_STORM" $(FASE45_FORK_ROLLBACK_STORM_LOG); then \
		echo "✓ smoke-fase45-fork-rollback-storm finished"; \
	else \
		echo "✗ smoke-fase45-fork-rollback-storm FAILED"; \
		exit 1; \
	fi

smoke-fase45-fork-mem-touch: build-init-fase45-fork-mem-touch kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) create-disk; \
	fi
	@echo "  SMOKE   FASE45 fork mem touch (512)..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	$(SMOKE_QEMU_RUN) --log $(FASE45_FORK_MEM_TOUCH_LOG) --timeout 180 --done FASE45_FORK_MEM_TOUCH -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE45_FORK_MEM_TOUCH" $(FASE45_FORK_MEM_TOUCH_LOG); then \
		echo "✓ smoke-fase45-fork-mem-touch finished"; \
	else \
		echo "✗ smoke-fase45-fork-mem-touch FAILED"; \
		exit 1; \
	fi

smoke-fase46-fork-no-recursion: build-init-fase46-fork-no-recursion kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) create-disk; \
	fi
	@echo "  SMOKE   FASE46 fork-no-recursion (512)..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	$(SMOKE_QEMU_RUN) --log $(FASE46_FORK_NO_RECURSE_LOG) --timeout 180 --done FASE46_FORK_NO_RECURSE -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE46_FORK_NO_RECURSE" $(FASE46_FORK_NO_RECURSE_LOG); then \
		echo "✓ smoke-fase46-fork-no-recursion finished"; \
	else \
		echo "✗ smoke-fase46-fork-no-recursion FAILED"; \
		exit 1; \
	fi

smoke-fase46-fork-heap: build-init-fase46-fork-heap kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) create-disk; \
	fi
	@echo "  SMOKE   FASE46 fork+heap (256)..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	$(SMOKE_QEMU_RUN) --log $(FASE46_FORK_HEAP_LOG) --timeout 180 --done FASE46_FORK_HEAP -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE46_FORK_HEAP" $(FASE46_FORK_HEAP_LOG); then \
		echo "✓ smoke-fase46-fork-heap finished"; \
	else \
		echo "✗ smoke-fase46-fork-heap FAILED"; \
		exit 1; \
	fi

smoke-fase48-ipc: build-init-fase48-ipc build-fase48-ipc-bins kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) create-disk; \
	fi
	@echo "  SMOKE   FASE48 IPC (pipe/dup/exec/pipeline/busybox)..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	python3 scripts/inject_init_minix.py $$DISK $(FASE48_CAT_BIN) bin/cat; \
	python3 scripts/inject_init_minix.py $$DISK $(FASE48_ECHO_BIN) bin/echo; \
	python3 scripts/inject_init_minix.py $$DISK $(FASE48_BUSYBOX_BIN) bin/busybox; \
	$(SMOKE_QEMU_RUN) --log $(FASE48_IPC_LOG) --timeout 180 --done ipc_class=IPC_READY -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE48_IPC" $(FASE48_IPC_LOG) && \
	    grep -q "pingpong=OK" $(FASE48_IPC_LOG) && \
	    grep -q "pipe_exec=OK" $(FASE48_IPC_LOG) && \
	    grep -q "pipeline=OK" $(FASE48_IPC_LOG) && \
	    grep -q "FASE48_BUSYBOX_PROBE_OK" $(FASE48_IPC_LOG) && \
	    grep -q "ipc_class=IPC_READY" $(FASE48_IPC_LOG); then \
		echo "✓ smoke-fase48-ipc finished"; \
	else \
		echo "✗ smoke-fase48-ipc FAILED"; \
		exit 1; \
	fi

smoke-fase49-pipe: build-init-fase49-pipe build-fase48-ipc-bins kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) create-disk; \
	fi
	@echo "  SMOKE   FASE49 pipe (EOF/FD lifetime/pipeline)..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	python3 scripts/inject_init_minix.py $$DISK $(FASE48_CAT_BIN) bin/cat; \
	python3 scripts/inject_init_minix.py $$DISK $(FASE48_ECHO_BIN) bin/echo; \
	$(SMOKE_QEMU_RUN) --log $(FASE49_PIPE_LOG) --timeout 180 --done pipe_class=PIPE_READY -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE49_PIPE" $(FASE49_PIPE_LOG) && \
	    grep -q "check1=OK" $(FASE49_PIPE_LOG) && \
	    grep -q "check2=OK" $(FASE49_PIPE_LOG) && \
	    grep -q "check3=OK" $(FASE49_PIPE_LOG) && \
	    grep -q "check4=OK" $(FASE49_PIPE_LOG) && \
	    grep -q "check5=OK" $(FASE49_PIPE_LOG) && \
	    grep -q "check6=OK" $(FASE49_PIPE_LOG) && \
	    grep -q "pipe_class=PIPE_READY" $(FASE49_PIPE_LOG); then \
		echo "✓ smoke-fase49-pipe finished"; \
	else \
		echo "✗ smoke-fase49-pipe FAILED"; \
		exit 1; \
	fi

smoke-fase50-busybox: build-init-fase50-busybox build-busybox-fase50-min kernel-x64-userspace.iso
	@echo "  SMOKE   FASE50 BusyBox (real applets)..."
	@strings $(INIT_SMOKE_BIN) 2>/dev/null | grep -q "FASE50_BUSYBOX_HARNESS_ID" || \
		(echo "✗ $(INIT_SMOKE_BIN) is not FASE50 harness — run build-init-fase50-busybox"; exit 1)
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=200 status=none && \
	python3 scripts/inject_init_minix.py --format $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init && \
	python3 scripts/inject_init_minix.py $$DISK $(FASE50_BUSYBOX_BIN) bin/busybox && \
	python3 scripts/inject_init_minix.py $$DISK $(FASE50_BUSYBOX_BIN) bin/cat && \
	python3 scripts/inject_init_minix.py $$DISK $(FASE50_BUSYBOX_BIN) bin/sh && \
	python3 scripts/verify_minix_rootfs.py $$DISK /bin /bin/busybox /bin/cat && \
	$(SMOKE_QEMU_RUN) --log $(FASE50_BUSYBOX_LOG) --profile fase50-busybox --done FASE50E_NO_REGRESSION -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE51_" $(FASE50_BUSYBOX_LOG) && \
	    ! grep -q "FASE50_BUSYBOX_HARNESS_ID" $(FASE50_BUSYBOX_LOG); then \
		echo "✗ FASE50D_FLAKE_HARNESS_INIT_BIN_COLLISION (FASE51 init in FASE50 log)"; \
		exit 1; \
	fi
	@if grep -q "BUSYBOX_BOOT_OK" $(FASE50_BUSYBOX_LOG) && \
	    grep -q "FASE50_BUSYBOX_HARNESS_ID" $(FASE50_BUSYBOX_LOG) && \
	    grep -q "FASE50_BUSYBOX_COREUTILS_MINIMAL_OK" $(FASE50_BUSYBOX_LOG) && \
	    grep -q "FASE50D_TANDA2_OK" $(FASE50_BUSYBOX_LOG) && \
	    grep -q "FASE50D_TANDA3_OK" $(FASE50_BUSYBOX_LOG) && \
	    grep -q "FASE50E_OK" $(FASE50_BUSYBOX_LOG) && \
	    grep -q "FASE50E_BASELINE_STABLE" $(FASE50_BUSYBOX_LOG) && \
	    grep -q "FASE50E_NO_REGRESSION" $(FASE50_BUSYBOX_LOG); then \
		echo "✓ smoke-fase50-busybox finished"; \
	else \
		echo "✗ smoke-fase50-busybox FAILED"; \
		if grep -q "BUSYBOX_FAIL_REASON" $(FASE50_BUSYBOX_LOG); then \
			grep "BUSYBOX_FAIL_REASON" $(FASE50_BUSYBOX_LOG); \
		fi; \
		if grep -q "\[FASE50D\]\[FAIL\]" $(FASE50_BUSYBOX_LOG); then \
			grep "\[FASE50D\]\[FAIL\]" $(FASE50_BUSYBOX_LOG); \
		fi; \
		if grep -q "\[FASE50D\]\[CLASSIFY\]" $(FASE50_BUSYBOX_LOG); then \
			echo "--- FASE50D classify ---"; \
			grep "\[FASE50D\]\[CLASSIFY\]" $(FASE50_BUSYBOX_LOG); \
		fi; \
		if grep -q "EXEC_VFS_READ_ERR" $(FASE50_BUSYBOX_LOG); then \
			echo "--- kernel EXEC_VFS_READ_ERR (minix read flake) ---"; \
			grep "EXEC_VFS_READ_ERR" $(FASE50_BUSYBOX_LOG) | tail -3; \
		fi; \
		exit 1; \
	fi

smoke-fase50-exec-only: build-init-fase50-exec-only build-busybox-fase50-min kernel-x64-userspace.iso
	@echo "  SMOKE   FASE50 EXEC-only (busybox read/exec loop, N=50)..."
	@strings $(INIT_SMOKE_BIN) 2>/dev/null | grep -q "EXEC_ONLY_HARNESS_ID" || \
		(echo "✗ $(INIT_SMOKE_BIN) is not EXEC-only harness — run build-init-fase50-exec-only"; exit 1)
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=200 status=none && \
	python3 scripts/inject_init_minix.py --format $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init && \
	python3 scripts/inject_init_minix.py $$DISK $(FASE50_BUSYBOX_BIN) bin/busybox && \
	python3 scripts/verify_minix_rootfs.py $$DISK /bin /bin/busybox && \
	$(SMOKE_QEMU_RUN) --log $(FASE50_EXEC_ONLY_LOG) --profile fase50-exec-only \
		--done EXEC_ONLY_STABLE_OK --fail-regex 'EXEC_ONLY_FAIL|_FAIL_REASON|\[FASE[0-9A-Z]+\]\[FAIL\]' -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "EXEC_ONLY_STABLE_OK" $(FASE50_EXEC_ONLY_LOG) && \
	    grep -q "EXEC_ONLY_HARNESS_ID" $(FASE50_EXEC_ONLY_LOG) && \
	    grep -qE "EXEC_ONLY_N50_STABLE_OK|EXEC_BUSYBOX_READ_STABLE_FIXED" $(FASE50_EXEC_ONLY_LOG); then \
		echo "✓ smoke-fase50-exec-only finished"; \
	elif grep -q "EXEC_ONLY_FAIL" $(FASE50_EXEC_ONLY_LOG); then \
		echo "✗ smoke-fase50-exec-only REPRO (classified)"; \
		grep "EXEC_ONLY_FAIL" $(FASE50_EXEC_ONLY_LOG) | tail -1; \
		if grep -q "\[EXEC_ONLY\]\[CLASSIFY\]" $(FASE50_EXEC_ONLY_LOG); then \
			echo "--- EXEC_ONLY classify ---"; \
			grep "\[EXEC_ONLY\]\[CLASSIFY\]" $(FASE50_EXEC_ONLY_LOG) | sort -u; \
		fi; \
		if grep -q "MINIX_READ_EIO_AT_BLOCK\|DEVICE_READ_FLAKE" $(FASE50_EXEC_ONLY_LOG); then \
			echo "--- block/device ---"; \
			grep -E "\[EXEC_ONLY\]\[MINIX\]|\[EXEC_ONLY\]\[DEV\]" $(FASE50_EXEC_ONLY_LOG) | tail -12; \
		fi; \
		exit 1; \
	else \
		echo "✗ smoke-fase50-exec-only FAILED (harness incomplete)"; \
		exit 1; \
	fi

smoke-fase51-shell: build-init-fase51-shell build-busybox-fase50-min kernel-x64-userspace.iso
	@echo "  SMOKE   FASE51 BusyBox ash (pipes/redirect/loops)..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=200 status=none && \
	python3 scripts/inject_init_minix.py --format $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init && \
	python3 scripts/inject_init_minix.py $$DISK $(FASE50_BUSYBOX_BIN) bin/busybox && \
	python3 scripts/inject_init_minix.py $$DISK $(FASE50_BUSYBOX_BIN) bin/cat && \
	python3 scripts/inject_init_minix.py $$DISK $(FASE50_BUSYBOX_BIN) bin/sh && \
	python3 scripts/verify_minix_rootfs.py $$DISK /bin /bin/busybox /bin/cat && \
	$(SMOKE_QEMU_RUN) --log $(FASE51_SHELL_LOG) --profile fase51-shell --done DEBUG_FASE51_GATED -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE51_OK" $(FASE51_SHELL_LOG) && \
	    grep -q "FASE51_PIPE_OK" $(FASE51_SHELL_LOG) && \
	    grep -q "FASE51_REDIRECT_OK" $(FASE51_SHELL_LOG) && \
	    grep -q "FASE51_FOR_LOOP_OK" $(FASE51_SHELL_LOG) && \
	    grep -q "FASE51_CONTROL_FLOW_OK" $(FASE51_SHELL_LOG) && \
	    grep -q "FASE51_BASELINE_STABLE" $(FASE51_SHELL_LOG) && \
	    grep -q "DEBUG_FASE51_GATED" $(FASE51_SHELL_LOG); then \
		echo "✓ smoke-fase51-shell finished"; \
	else \
		echo "✗ smoke-fase51-shell FAILED"; \
		if grep -q "FASE51_FAIL_REASON" $(FASE51_SHELL_LOG); then \
			grep "FASE51_FAIL_REASON" $(FASE51_SHELL_LOG); \
		fi; \
		if grep -q "\[FASE51\]\[FAIL\]" $(FASE51_SHELL_LOG); then \
			grep "\[FASE51\]\[FAIL\]" $(FASE51_SHELL_LOG); \
		fi; \
		exit 1; \
	fi

smoke-fase52-tcc: build-runit build-init-fase52-tcc build-tcc-fase52 $(KERNEL_USERSPACE_ISO)
	@echo "  SMOKE   FASE52 TinyCC bootstrap (runit → fase52 service)..."
	@test -f $(FASE52_HARNESS_BIN) || (echo "✗ missing $(FASE52_HARNESS_BIN) — run build-init-fase52-tcc"; exit 1)
	@strings $(FASE52_HARNESS_BIN) 2>/dev/null | grep -q "FASE52_TCC_HARNESS_ID" || \
		(echo "✗ $(FASE52_HARNESS_BIN) is not FASE52 harness"; exit 1)
	@test -d $(FASE52_TCC_STAGE)/bin || (echo "✗ missing $(FASE52_TCC_STAGE) — run build-tcc-fase52"; exit 1)
	@test -f $(RUNIT_STAGE_BIN)/runit_fase52_run || (echo "✗ missing runit_fase52_run — run build-runit"; exit 1)
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=200 status=none && \
	python3 scripts/inject_init_minix.py --format-large $$DISK && \
	FASE50_BUSYBOX_BIN=$(FASE50_BUSYBOX_BIN) ./setup/runit/install-to-disk.sh $$DISK && \
	chmod +x setup/runit/inject-smoke-service.sh && \
	./setup/runit/inject-smoke-service.sh $$DISK fase52 \
		$(RUNIT_STAGE_BIN)/runit_fase52_run $(FASE52_HARNESS_BIN) bin/f52-harness && \
	find $(FASE52_TCC_STAGE) -type f | sort | while read -r f; do \
		rel="$${f#$(FASE52_TCC_STAGE)/}"; \
		python3 scripts/inject_init_minix.py $$DISK "$$f" "$$rel"; \
	done && \
	python3 scripts/verify_minix_rootfs.py $$DISK /sbin/init /bin/f52-harness /bin/tcc \
		/etc/runit/sv/fase52/run && \
	$(SMOKE_QEMU_RUN) --log $(FASE52_TCC_LOG) --profile fase52-tcc --done FASE52_OK -- \
		$(QEMU) -cdrom $(KERNEL_USERSPACE_ISO) \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "RUNIT_STAGE2_OK" $(FASE52_TCC_LOG) && \
	    grep -q "RUNSV_FASE52_START" $(FASE52_TCC_LOG) && \
	    grep -q "FASE52_OK" $(FASE52_TCC_LOG) && \
	    grep -q "FASE52_TCC_HARNESS_ID" $(FASE52_TCC_LOG) && \
	    grep -q "FASE52_TCC_BOOT_OK" $(FASE52_TCC_LOG) && \
	    grep -q "FASE52B_TCC_STATIC_LINK_OK" $(FASE52_TCC_LOG) && \
	    grep -q "FASE52B_TCC_EXEC_GENERATED_OK" $(FASE52_TCC_LOG) && \
	    grep -q "FASE52B_TCC_STDIO_HELLO_OK" $(FASE52_TCC_LOG) && \
	    grep -q "FASE52B_BASE_LAYOUT_OK" $(FASE52_TCC_LOG) && \
	    grep -q "FASE52C_PRINTF_OK" $(FASE52_TCC_LOG) && \
	    grep -q "FASE52C_MALLOC_OK" $(FASE52_TCC_LOG) && \
	    grep -q "FASE52C_STDIO_FILE_OK" $(FASE52_TCC_LOG) && \
	    grep -q "FASE52C_MULTI_OBJECT_LINK_OK" $(FASE52_TCC_LOG) && \
	    grep -q "FASE52C_OK" $(FASE52_TCC_LOG) && \
	    grep -q "FASE52D_OK" $(FASE52_TCC_LOG) && \
	    grep -q "FASE52D_MEDIUM_PROGRAM_OK" $(FASE52_TCC_LOG) && \
	    grep -q "FASE52D_LARGE_FILE_PROGRAM_OK" $(FASE52_TCC_LOG) && \
	    grep -q "VFS_OFFSET_READ_OK\|MINIX_OFFSET_READ_OK" $(FASE52_TCC_LOG) && \
	    grep -q "LARGE_FILE_RW_OK" $(FASE52_TCC_LOG); then \
		echo "✓ smoke-fase52-tcc finished"; \
	elif grep -q "FASE52_FAIL_REASON" $(FASE52_TCC_LOG); then \
		echo "✗ smoke-fase52-tcc REPRO (classified)"; \
		grep "FASE52_FAIL_REASON" $(FASE52_TCC_LOG) | tail -1; \
		grep "\[FASE52\]\[CLASSIFY\]" $(FASE52_TCC_LOG) | sort -u; \
		exit 1; \
	else \
		echo "✗ smoke-fase52-tcc FAILED (harness incomplete)"; \
		exit 1; \
	fi

smoke-fase53a-fs-dev: build-init-fase53a-fs-dev build-busybox-fase50-min build-tcc-fase52 kernel-x64-userspace.iso
	@echo "  SMOKE   FASE53A userspace base (/dev + /tmp + cwd + tcc)..."
	@strings $(INIT_SMOKE_BIN) 2>/dev/null | grep -q "FASE53A_FS_DEV_HARNESS_ID" || \
		(echo "✗ $(INIT_SMOKE_BIN) is not FASE53A harness — run build-init-fase53a-fs-dev"; exit 1)
	@test -d $(FASE52_TCC_STAGE)/bin || (echo "✗ missing $(FASE52_TCC_STAGE) — run build-tcc-fase52"; exit 1)
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=200 status=none && \
	python3 scripts/inject_init_minix.py --format-large $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init && \
	python3 scripts/inject_init_minix.py $$DISK $(FASE50_BUSYBOX_BIN) bin/busybox && \
	python3 scripts/inject_init_minix.py $$DISK $(FASE50_BUSYBOX_BIN) bin/sh && \
	find $(FASE52_TCC_STAGE) -type f | sort | while read -r f; do \
		rel="$${f#$(FASE52_TCC_STAGE)/}"; \
		python3 scripts/inject_init_minix.py $$DISK "$$f" "$$rel"; \
	done && \
	python3 scripts/verify_minix_rootfs.py $$DISK /bin /bin/busybox /bin/tcc && \
	$(SMOKE_QEMU_RUN) --log $(FASE53A_FS_DEV_LOG) --profile fase53a-fs-dev --done FASE53A_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE53A_OK" $(FASE53A_FS_DEV_LOG) && \
	    grep -q "FASE53A_FS_DEV_HARNESS_ID" $(FASE53A_FS_DEV_LOG) && \
	    grep -q "FASE53A_ROOTFS_LAYOUT_OK" $(FASE53A_FS_DEV_LOG) && \
	    grep -q "DEVFS_NULL_OK" $(FASE53A_FS_DEV_LOG) && \
	    grep -q "DEVFS_ZERO_OK" $(FASE53A_FS_DEV_LOG) && \
	    grep -q "TMPDIR_OK" $(FASE53A_FS_DEV_LOG) && \
	    grep -q "CWD_CHDIR_OK" $(FASE53A_FS_DEV_LOG) && \
	    grep -q "TCC_LAYOUT_NO_REGRESSION" $(FASE53A_FS_DEV_LOG) && \
	    grep -q "FASE50_51_52_NO_REGRESSION" $(FASE53A_FS_DEV_LOG); then \
		echo "✓ smoke-fase53a-fs-dev finished"; \
	else \
		echo "✗ smoke-fase53a-fs-dev FAILED"; \
		if grep -q "FASE53A_FAIL_REASON" $(FASE53A_FS_DEV_LOG); then \
			grep "FASE53A_FAIL_REASON" $(FASE53A_FS_DEV_LOG); \
		fi; \
		if grep -q "\[FASE53A\]\[FAIL\]" $(FASE53A_FS_DEV_LOG); then \
			grep "\[FASE53A\]\[FAIL\]" $(FASE53A_FS_DEV_LOG); \
		fi; \
		exit 1; \
	fi

smoke-fase53b-posix-pseudofs: build-init-fase53b-posix-pseudofs build-busybox-fase50-min kernel-x64-userspace.iso
	@echo "  WARN    DEPRECATED: prefer 'make ktm-userdev-posix-pseudofs-virtfs-run'"
	@echo "  SMOKE   FASE53B POSIX pseudo-fs routed path..."
	@strings $(INIT_SMOKE_BIN) 2>/dev/null | grep -q "FASE53B_POSIX_PSEUDOFS_HARNESS_ID" || \
		(echo "✗ $(INIT_SMOKE_BIN) is not FASE53B harness — run build-init-fase53b-posix-pseudofs"; exit 1)
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=200 status=none && \
	python3 scripts/inject_init_minix.py --format-large $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init && \
	python3 scripts/inject_init_minix.py $$DISK $(FASE50_BUSYBOX_BIN) bin/busybox && \
	python3 scripts/inject_init_minix.py $$DISK $(FASE50_BUSYBOX_BIN) bin/sh && \
	python3 scripts/verify_minix_rootfs.py $$DISK /bin /bin/busybox && \
	$(SMOKE_QEMU_RUN) --log $(FASE53B_POSIX_PSEUDOFS_LOG) --profile fase53b-posix --done FASE53B_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE53B_OK" $(FASE53B_POSIX_PSEUDOFS_LOG) && \
	    grep -q "FASE53B_POSIX_PSEUDOFS_HARNESS_ID" $(FASE53B_POSIX_PSEUDOFS_LOG) && \
	    grep -q "FASE53B_ROUTED_PATH_OK" $(FASE53B_POSIX_PSEUDOFS_LOG) && \
	    grep -q "FASE53B_FACCESSAT_OK" $(FASE53B_POSIX_PSEUDOFS_LOG) && \
	    grep -q "FASE53B_GETDENTS_CURSOR_OK" $(FASE53B_POSIX_PSEUDOFS_LOG) && \
	    grep -q "FASE53B_PSEUDOFS_NO_DUP_OK" $(FASE53B_POSIX_PSEUDOFS_LOG); then \
		echo "✓ smoke-fase53b-posix-pseudofs finished"; \
	else \
		echo "✗ smoke-fase53b-posix-pseudofs FAILED"; \
		if grep -q "FASE53B_FAIL_REASON" $(FASE53B_POSIX_PSEUDOFS_LOG); then \
			grep "FASE53B_FAIL_REASON" $(FASE53B_POSIX_PSEUDOFS_LOG); \
		fi; \
		if grep -q "\[FASE53B\]\[FAIL\]" $(FASE53B_POSIX_PSEUDOFS_LOG); then \
			grep "\[FASE53B\]\[FAIL\]" $(FASE53B_POSIX_PSEUDOFS_LOG); \
		fi; \
		exit 1; \
	fi

smoke-heart: build-init-heart-smoke kernel-x64-userspace.iso
	@echo "  SMOKE   /heart facade + src..."
	@strings $(INIT_SMOKE_BIN) 2>/dev/null | grep -q "HEART_HARNESS_ID" || \
		(echo "✗ $(INIT_SMOKE_BIN) is not HEART harness — run build-init-heart-smoke"; exit 1)
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=64 status=none && \
	python3 scripts/inject_init_minix.py --format-large $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init && \
	$(SMOKE_QEMU_RUN) --log $(HEART_SMOKE_LOG) --timeout 60 --done HEART_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "HEART_OK" $(HEART_SMOKE_LOG) && \
	    grep -q "HEART_PROC_MIRROR_OK" $(HEART_SMOKE_LOG) && \
	    grep -q "HEART_SYS_MIRROR_OK" $(HEART_SMOKE_LOG) && \
	    grep -q "HEART_KERNEL_META_OK" $(HEART_SMOKE_LOG) && \
	    grep -q "HEART_SRC_OK" $(HEART_SMOKE_LOG) && \
	    grep -q "HEART_SRC_EXPAND_OK" $(HEART_SMOKE_LOG) && \
	    grep -q "HEART_PROC_CMDLINE_OK" $(HEART_SMOKE_LOG) && \
	    grep -q "HEART_SYS_OSRELEASE_OK" $(HEART_SMOKE_LOG) && \
	    grep -q "HEART_DUP_OK" $(HEART_SMOKE_LOG); then \
		echo "✓ smoke-heart finished"; \
	else \
		echo "✗ smoke-heart FAILED"; \
		if grep -q "HEART_FAIL_REASON" $(HEART_SMOKE_LOG); then \
			grep "HEART_FAIL_REASON" $(HEART_SMOKE_LOG); \
		fi; \
		if grep -q "\[HEART\]\[FAIL\]" $(HEART_SMOKE_LOG); then \
			grep "\[HEART\]\[FAIL\]" $(HEART_SMOKE_LOG); \
		fi; \
		exit 1; \
	fi

smoke-fase54a-fbdev: build-init-fase54a-fbdev build-busybox-fase50-min kernel-x64-userspace.iso
	@echo "  SMOKE   FASE54A minimal fbdev slice..."
	@strings $(INIT_SMOKE_BIN) 2>/dev/null | grep -q "FASE54A_FBDEV_HARNESS_ID" || \
		(echo "✗ $(INIT_SMOKE_BIN) is not FASE54A harness — run build-init-fase54a-fbdev"; exit 1)
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=200 status=none && \
	python3 scripts/inject_init_minix.py --format-large $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init && \
	python3 scripts/inject_init_minix.py $$DISK $(FASE50_BUSYBOX_BIN) bin/busybox && \
	python3 scripts/inject_init_minix.py $$DISK $(FASE50_BUSYBOX_BIN) bin/sh && \
	python3 scripts/verify_minix_rootfs.py $$DISK /bin /bin/busybox && \
	$(SMOKE_QEMU_RUN) --log $(FASE54A_FBDEV_LOG) --profile fase54a-fbdev --done FASE54A_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE54A_OK" $(FASE54A_FBDEV_LOG) && \
	    grep -q "FASE54A_FBDEV_HARNESS_ID" $(FASE54A_FBDEV_LOG) && \
	    grep -q "DEVFS_FB0_OK" $(FASE54A_FBDEV_LOG) && \
	    grep -q "FB_FACADE_OK" $(FASE54A_FBDEV_LOG) && \
	    grep -q "FASE54A_FBDEV_PRESENT" $(FASE54A_FBDEV_LOG) && \
	    grep -q "FASE54A_FB_GETINFO_OK" $(FASE54A_FBDEV_LOG) && \
	    grep -q "FASE54A_FB_DRAW_OK" $(FASE54A_FBDEV_LOG); then \
		echo "✓ smoke-fase54a-fbdev finished"; \
	else \
		echo "✗ smoke-fase54a-fbdev FAILED"; \
		if grep -q "FASE54A_FAIL_REASON" $(FASE54A_FBDEV_LOG); then \
			grep "FASE54A_FAIL_REASON" $(FASE54A_FBDEV_LOG); \
		fi; \
		if grep -q "\[FASE54A\]\[FAIL\]" $(FASE54A_FBDEV_LOG); then \
			grep "\[FASE54A\]\[FAIL\]" $(FASE54A_FBDEV_LOG); \
		fi; \
		exit 1; \
	fi

smoke-fase54b-input: build-init-fase54b-input kernel-x64-userspace.iso
	@echo "  SMOKE   FASE54B fbdev + input minimal slice..."
	@strings $(INIT_SMOKE_BIN) 2>/dev/null | grep -q "FASE54B_INPUT_HARNESS_ID" || \
		(echo "✗ $(INIT_SMOKE_BIN) is not FASE54B harness — run build-init-fase54b-input"; exit 1)
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=200 status=none && \
	python3 scripts/inject_init_minix.py --format-large $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init && \
	python3 scripts/verify_minix_rootfs.py $$DISK /sbin/init && \
	$(SMOKE_QEMU_RUN) --log $(FASE54B_INPUT_LOG) --profile fase54b-input --done FASE54B_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE54B_OK" $(FASE54B_INPUT_LOG) && \
	    grep -q "FASE54B_INPUT_HARNESS_ID" $(FASE54B_INPUT_LOG) && \
	    grep -q "INPUT_FACADE_OK" $(FASE54B_INPUT_LOG) && \
	    grep -q "DEVFS_INPUT0_OK" $(FASE54B_INPUT_LOG) && \
	    grep -q "INPUT_EVENT_READ_OK" $(FASE54B_INPUT_LOG) && \
	    grep -q "FASE54B_FB_INTERACTIVE_OK" $(FASE54B_INPUT_LOG); then \
		echo "✓ smoke-fase54b-input finished"; \
	else \
		echo "✗ smoke-fase54b-input FAILED"; \
		if grep -q "FASE54B_FAIL_REASON" $(FASE54B_INPUT_LOG); then \
			grep "FASE54B_FAIL_REASON" $(FASE54B_INPUT_LOG); \
		fi; \
		if grep -q "\[FASE54B\]\[FAIL\]" $(FASE54B_INPUT_LOG); then \
			grep "\[FASE54B\]\[FAIL\]" $(FASE54B_INPUT_LOG); \
		fi; \
		exit 1; \
	fi

smoke-fase54c-input-deterministic: build-init-fase54c-input-deterministic kernel-x64-userspace.iso
	@echo "  WARN    DEPRECATED: prefer 'make ktm-userdev-input-det-virtfs-run'"
	@echo "  SMOKE   FASE54C deterministic input path..."
	@strings $(INIT_SMOKE_BIN) 2>/dev/null | grep -q "FASE54C_INPUT_DETERMINISTIC_HARNESS_ID" || \
		(echo "✗ $(INIT_SMOKE_BIN) is not FASE54C harness — run build-init-fase54c-input-deterministic"; exit 1)
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=200 status=none && \
	python3 scripts/inject_init_minix.py --format-large $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init && \
	python3 scripts/verify_minix_rootfs.py $$DISK /sbin/init && \
	$(SMOKE_QEMU_RUN) --log $(FASE54C_INPUT_DET_LOG) --profile fase54c-input-det --done FASE54C_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE54C_OK" $(FASE54C_INPUT_DET_LOG) && \
	    grep -q "FASE54C_INPUT_DETERMINISTIC_HARNESS_ID" $(FASE54C_INPUT_DET_LOG) && \
	    grep -q "INPUT_INJECT_TESTHOOK_OK" $(FASE54C_INPUT_DET_LOG) && \
	    grep -q "DEVFS_EVENTS0_READ_OK" $(FASE54C_INPUT_DET_LOG) && \
	    grep -q "INPUT_EVENT_READ_OK" $(FASE54C_INPUT_DET_LOG) && \
	    grep -q "FASE54C_INPUT_DETERMINISTIC_OK" $(FASE54C_INPUT_DET_LOG); then \
		echo "✓ smoke-fase54c-input-deterministic finished"; \
	else \
		echo "✗ smoke-fase54c-input-deterministic FAILED"; \
		if grep -q "FASE54C_FAIL_REASON" $(FASE54C_INPUT_DET_LOG); then \
			grep "FASE54C_FAIL_REASON" $(FASE54C_INPUT_DET_LOG); \
		fi; \
		if grep -q "\[FASE54C\]\[FAIL\]" $(FASE54C_INPUT_DET_LOG); then \
			grep "\[FASE54C\]\[FAIL\]" $(FASE54C_INPUT_DET_LOG); \
		fi; \
		exit 1; \
	fi

smoke-fase55a-doom-prereq: build-init-fase55a-doom-prereq build-busybox-fase50-min kernel-x64-userspace.iso
	@echo "  SMOKE   FASE55A doomgeneric prereq loop..."
	@strings $(INIT_SMOKE_BIN) 2>/dev/null | grep -q "FASE55A_DOOM_PREREQ_HARNESS_ID" || \
		(echo "✗ $(INIT_SMOKE_BIN) is not FASE55A harness — run build-init-fase55a-doom-prereq"; exit 1)
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=200 status=none && \
	python3 scripts/inject_init_minix.py --format-large $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init && \
	python3 scripts/inject_init_minix.py $$DISK $(FASE50_BUSYBOX_BIN) usr/share/doom/doom1.wad && \
	python3 scripts/verify_minix_rootfs.py $$DISK /sbin/init /usr/share/doom/doom1.wad && \
	$(SMOKE_QEMU_RUN) --log $(FASE55A_DOOM_PREREQ_LOG) --timeout 120 --done FASE55A_DOOM_PREREQ_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE55A_DOOM_PREREQ_HARNESS_ID" $(FASE55A_DOOM_PREREQ_LOG) && \
	    grep -q "DOOM_LOOP_FB_OPEN_OK" $(FASE55A_DOOM_PREREQ_LOG) && \
	    grep -q "DOOM_LOOP_INPUT_OPEN_OK" $(FASE55A_DOOM_PREREQ_LOG) && \
	    grep -q "DOOM_WAD_READ_OK" $(FASE55A_DOOM_PREREQ_LOG) && \
	    grep -q "DOOM_FRAME_DRAW_OK" $(FASE55A_DOOM_PREREQ_LOG) && \
	    grep -q "FASE55A_DOOM_PREREQ_OK" $(FASE55A_DOOM_PREREQ_LOG); then \
		echo "✓ smoke-fase55a-doom-prereq finished"; \
	else \
		echo "✗ smoke-fase55a-doom-prereq FAILED"; \
		if grep -q "FASE55A_FAIL_REASON" $(FASE55A_DOOM_PREREQ_LOG); then \
			grep "FASE55A_FAIL_REASON" $(FASE55A_DOOM_PREREQ_LOG); \
		fi; \
		if grep -q "\[FASE55A\]\[FAIL\]" $(FASE55A_DOOM_PREREQ_LOG); then \
			grep "\[FASE55A\]\[FAIL\]" $(FASE55A_DOOM_PREREQ_LOG); \
		fi; \
		exit 1; \
	fi

smoke-fase55b-doom-stub: build-init-fase55b-doom-stub build-busybox-fase50-min kernel-x64-userspace.iso
	@echo "  SMOKE   FASE55B doomgeneric-style stub..."
	@strings $(INIT_SMOKE_BIN) 2>/dev/null | grep -q "FASE55B_DOOM_STUB_HARNESS_ID" || \
		(echo "✗ $(INIT_SMOKE_BIN) is not FASE55B harness — run build-init-fase55b-doom-stub"; exit 1)
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=200 status=none && \
	python3 scripts/inject_init_minix.py --format-large $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init && \
	python3 scripts/inject_init_minix.py $$DISK $(FASE50_BUSYBOX_BIN) usr/share/doom/doom1.wad && \
	python3 scripts/verify_minix_rootfs.py $$DISK /sbin/init /usr/share/doom/doom1.wad && \
	$(SMOKE_QEMU_RUN) --log $(FASE55B_DOOM_STUB_LOG) --timeout 120 --done FASE55B_DOOM_STUB_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE55B_DOOM_STUB_HARNESS_ID" $(FASE55B_DOOM_STUB_LOG) && \
	    grep -q "DOOM_STUB_FB_OK" $(FASE55B_DOOM_STUB_LOG) && \
	    grep -q "DOOM_STUB_INPUT_OK" $(FASE55B_DOOM_STUB_LOG) && \
	    grep -q "DOOM_STUB_WAD_OK" $(FASE55B_DOOM_STUB_LOG) && \
	    grep -q "DOOM_STUB_FRAME_LOOP_OK" $(FASE55B_DOOM_STUB_LOG) && \
	    grep -q "FASE55B_DOOM_STUB_OK" $(FASE55B_DOOM_STUB_LOG); then \
		echo "✓ smoke-fase55b-doom-stub finished"; \
	else \
		echo "✗ smoke-fase55b-doom-stub FAILED"; \
		if grep -q "FASE55B_FAIL_REASON" $(FASE55B_DOOM_STUB_LOG); then \
			grep "FASE55B_FAIL_REASON" $(FASE55B_DOOM_STUB_LOG); \
		fi; \
		if grep -q "\[FASE55B\]\[FAIL\]" $(FASE55B_DOOM_STUB_LOG); then \
			grep "\[FASE55B\]\[FAIL\]" $(FASE55B_DOOM_STUB_LOG); \
		fi; \
		exit 1; \
	fi

smoke-fase55c-timing-input: build-init-fase55c-timing-input build-busybox-fase50-min kernel-x64-userspace.iso
	@echo "  SMOKE   FASE55C timed loop + input backend..."
	@strings $(INIT_SMOKE_BIN) 2>/dev/null | grep -q "FASE55B_DOOM_STUB_HARNESS_ID" || \
		(echo "✗ $(INIT_SMOKE_BIN) is not FASE55C/55B harness — run build-init-fase55c-timing-input"; exit 1)
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=200 status=none && \
	python3 scripts/inject_init_minix.py --format-large $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init && \
	python3 scripts/inject_init_minix.py $$DISK $(FASE50_BUSYBOX_BIN) usr/share/doom/doom1.wad && \
	python3 scripts/verify_minix_rootfs.py $$DISK /sbin/init /usr/share/doom/doom1.wad && \
	$(SMOKE_QEMU_RUN) --log $(FASE55C_TIMING_INPUT_LOG) --timeout 120 --done FASE55C_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "FASE55C_OK" $(FASE55C_TIMING_INPUT_LOG) && \
	    grep -q "CLOCK_MONOTONIC_OK" $(FASE55C_TIMING_INPUT_LOG) && \
	    grep -q "NANOSLEEP_OR_YIELD_OK" $(FASE55C_TIMING_INPUT_LOG) && \
	    grep -q "INPUT_PS2_CAPS_OK" $(FASE55C_TIMING_INPUT_LOG) && \
	    grep -q "INPUT_TEST_INJECT_STILL_OK" $(FASE55C_TIMING_INPUT_LOG) && \
	    grep -q "DOOM_STUB_TIMED_LOOP_OK" $(FASE55C_TIMING_INPUT_LOG); then \
		echo "✓ smoke-fase55c-timing-input finished"; \
	else \
		echo "✗ smoke-fase55c-timing-input FAILED"; \
		if grep -q "FASE55B_FAIL_REASON" $(FASE55C_TIMING_INPUT_LOG); then \
			grep "FASE55B_FAIL_REASON" $(FASE55C_TIMING_INPUT_LOG); \
		fi; \
		if grep -q "\[FASE55B\]\[FAIL\]" $(FASE55C_TIMING_INPUT_LOG); then \
			grep "\[FASE55B\]\[FAIL\]" $(FASE55C_TIMING_INPUT_LOG); \
		fi; \
		exit 1; \
	fi

smoke-fase55d-doomgeneric: build-runit build-init-fase55d-doomgeneric kernel-x64-userspace.iso
	@echo "  SMOKE   FASE55D doomgeneric real incremental (runit → doom service)..."
	@if [ -z "$(REAL_WAD_PATH)" ] || [ ! -f "$(REAL_WAD_PATH)" ]; then \
		echo "✗ REAL_WAD_PATH must point to a real WAD file"; \
		echo "  example: make smoke-fase55d-doomgeneric REAL_WAD_PATH=/path/to/doom1.wad"; \
		exit 1; \
	fi
	@test -f $(FASE55D_SMOKE_BIN) || (echo "✗ missing $(FASE55D_SMOKE_BIN) — run build-init-fase55d-doomgeneric"; exit 1)
	@test -f $(RUNIT_STAGE_BIN)/runit_fase55d_run || (echo "✗ missing runit_fase55d_run — run build-runit"; exit 1)
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=200 status=none && \
	python3 scripts/inject_init_minix.py --format-large $$DISK && \
	FASE50_BUSYBOX_BIN=$(FASE50_BUSYBOX_BIN) ./setup/runit/install-to-disk.sh $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(RUNIT_STAGE_BIN)/runit_fase55d_init sbin/init && \
	chmod +x setup/runit/inject-smoke-service.sh && \
	./setup/runit/inject-smoke-service.sh $$DISK doom \
		$(RUNIT_STAGE_BIN)/runit_fase55d_run $(FASE55D_SMOKE_BIN) bin/doom-smoke && \
	python3 scripts/inject_init_minix.py $$DISK "$(REAL_WAD_PATH)" usr/share/doom/doom1.wad && \
	python3 scripts/verify_minix_rootfs.py $$DISK /sbin/init /bin/doom-smoke \
		/usr/share/doom/doom1.wad /etc/runit/sv/doom/run && \
	$(SMOKE_QEMU_RUN) --log $(FASE55D_DOOMGENERIC_LOG) --profile fase55d-doom \
		--done FASE55D_DOOMGENERIC_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "RUNIT_STAGE2_OK" $(FASE55D_DOOMGENERIC_LOG) && \
	    grep -q "RUNSV_FASE55D_START" $(FASE55D_DOOMGENERIC_LOG) && \
	    grep -q "DOOMGENERIC_BUILD_OK" $(FASE55D_DOOMGENERIC_LOG) && \
	    grep -q "DOOMGENERIC_WAD_LOAD_OK" $(FASE55D_DOOMGENERIC_LOG) && \
	    grep -q "DOOMGENERIC_INIT_OK" $(FASE55D_DOOMGENERIC_LOG) && \
	    grep -q "DOOMGENERIC_FIRST_FRAME_OK" $(FASE55D_DOOMGENERIC_LOG) && \
	    grep -q "DOOMGENERIC_FRAME_LOOP_OK" $(FASE55D_DOOMGENERIC_LOG) && \
	    grep -q "FASE55D_DOOMGENERIC_OK" $(FASE55D_DOOMGENERIC_LOG) && \
	    grep -q "KTM_DOOM_55D_OK" $(FASE55D_DOOMGENERIC_LOG) && \
	    grep -q "KTM_USERDEV_OK" $(FASE55D_DOOMGENERIC_LOG); then \
		echo "LONG_RUNNING_BUT_STABLE"; \
		echo "FASE55D_DOOMGENERIC_REAL_WAD_OK"; \
		echo "✓ smoke-fase55d-doomgeneric finished"; \
	else \
		if grep -q "DOOMGENERIC_FRAME_LOOP_OK" $(FASE55D_DOOMGENERIC_LOG) && \
		   grep -q "FASE55D_DOOMGENERIC_OK" $(FASE55D_DOOMGENERIC_LOG); then \
			echo "LONG_RUNNING_BUT_STABLE"; \
		fi; \
		echo "✗ smoke-fase55d-doomgeneric FAILED"; \
		if grep -q "\[FASE55D\]\[FAIL\]" $(FASE55D_DOOMGENERIC_LOG); then \
			grep "\[FASE55D\]\[FAIL\]" $(FASE55D_DOOMGENERIC_LOG); \
		fi; \
		exit 1; \
	fi

# Hybrid KTM name for critical battery (same runit+WAD recipe)
ktm-userdev-doom-55d-run: smoke-fase55d-doomgeneric
	@echo "✓ ktm-userdev-doom-55d-run (hybrid runit)"

run-fase55d-doomgeneric-gui: build-fase55e-doom-interactive kernel-x64-userspace.iso
	@if [ -z "$(REAL_WAD_PATH)" ] || [ ! -f "$(REAL_WAD_PATH)" ]; then \
		echo "✗ REAL_WAD_PATH must point to a real WAD file"; \
		echo "  example: make run-fase55d-doomgeneric-gui REAL_WAD_PATH=/path/to/doom1.wad"; \
		exit 1; \
	fi
	@echo "  RUN     FASE55E Doom interactive (DOOM_FRAMES=$(DOOM_FRAMES) dump_every=$(DOOM_FRAME_DUMP_EVERY) display=$(DOOM_DISPLAY))..."
	@echo "  LOG     serial -> $(FASE55E_DOOM_GUI_LOG)"
	@echo "  HINT    ESC to quit Doom loop; Ctrl+C QEMU to stop VM"
	@rm -f $(FASE55E_DOOM_GUI_LOG); \
	DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	CFG=$$(mktemp /tmp/doom-frames-cfg.XXXXXX); \
	printf '%s\n%s\n' "$(DOOM_FRAMES)" "$(DOOM_FRAME_DUMP_EVERY)" > $$CFG; \
	dd if=/dev/zero of=$$DISK bs=1M count=200 status=none && \
	python3 scripts/inject_init_minix.py --format-large $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(FASE55E_DOOM_BIN) sbin/init && \
	python3 scripts/inject_init_minix.py $$DISK "$(REAL_WAD_PATH)" usr/share/doom/doom1.wad && \
	python3 scripts/inject_init_minix.py $$DISK $$CFG etc/doom-frames && \
	python3 scripts/verify_minix_rootfs.py $$DISK /sbin/init /usr/share/doom/doom1.wad /etc/doom-frames && \
	rm -f $$CFG; \
	if [ "$(DOOM_DISPLAY)" = "sdl" ]; then \
		DISP="-display sdl2"; \
	else \
		DISP="-display gtk"; \
	fi; \
	$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial file:$(FASE55E_DOOM_GUI_LOG) \
		$$DISP -m 256M -no-reboot -net none; \
	rm -f $$DISK

# FASE58C — framebuffer visibility diagnostics (GUI only, no console/process fixes)
run-fase58c-boot-gui: build-fase58c-boot-halt kernel-x64-userspace.iso
	@case "$(FASE58C_DISPLAY)" in none|headless) \
		echo "✗ FASE58C boot GUI blocked: FASE58C_DISPLAY=$(FASE58C_DISPLAY)"; exit 1;; esac
	@echo "  FASE58C A  kernel RGB bands before userspace (halt init, no FB overwrite)"
	@echo "  QEMU     display=$(FASE58C_DISPLAY) gfxpayload=1024x768x32 (grub.cfg)"
	@echo "  LOG      serial -> $(FASE58C_BOOT_LOG)"
	@echo "  EXPECT   GUI: red/green/blue horizontal bands | serial: FB_BOOT_DIRECT_DRAW_OK"
	@rm -f $(FASE58C_BOOT_LOG); \
	DISK=$$(mktemp /tmp/ir0-fase58c-boot.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=200 status=none && \
	python3 scripts/inject_init_minix.py --format-large $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(FASE58C_BOOT_BIN) sbin/init && \
	python3 scripts/verify_minix_rootfs.py $$DISK /sbin/init; \
	if [ "$(FASE58C_DISPLAY)" = "sdl" ]; then \
		DISP="-display sdl2"; \
	else \
		DISP="-display gtk"; \
	fi; \
	$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial file:$(FASE58C_BOOT_LOG) \
		$$DISP -m 256M -no-reboot -net none; \
	rm -f $$DISK

run-fase58c-fbdev-gui: build-fase58c-fbdev kernel-x64-userspace.iso
	@case "$(FASE58C_DISPLAY)" in none|headless) \
		echo "✗ FASE58C fbdev GUI blocked: FASE58C_DISPLAY=$(FASE58C_DISPLAY)"; exit 1;; esac
	@echo "  FASE58C B  /dev/fb0 mmap draw (cyan/magenta/yellow bands, no Doom/BusyBox/TTY)"
	@echo "  QEMU     display=$(FASE58C_DISPLAY) gfxpayload=1024x768x32 (grub.cfg)"
	@echo "  LOG      serial -> $(FASE58C_FBDEV_LOG)"
	@echo "  EXPECT   GUI: CMY bands | serial: DEVFB0_DRAW_OK"
	@rm -f $(FASE58C_FBDEV_LOG); \
	DISK=$$(mktemp /tmp/ir0-fase58c-fbdev.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=200 status=none && \
	python3 scripts/inject_init_minix.py --format-large $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(FASE58C_FBDEV_BIN) sbin/init && \
	python3 scripts/verify_minix_rootfs.py $$DISK /sbin/init; \
	if [ "$(FASE58C_DISPLAY)" = "sdl" ]; then \
		DISP="-display sdl2"; \
	else \
		DISP="-display gtk"; \
	fi; \
	$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial file:$(FASE58C_FBDEV_LOG) \
		$$DISP -m 256M -no-reboot -net none; \
	rm -f $$DISK

run-fase58c-doom-gui: build-fase55e-doom-interactive kernel-x64-userspace.iso
	@if [ ! -f "$(REAL_WAD_PATH)" ]; then \
		echo "✗ REAL_WAD_PATH missing: $(REAL_WAD_PATH)"; exit 1; \
	fi
	@case "$(FASE58C_DISPLAY)" in none|headless) \
		echo "✗ FASE58C doom GUI blocked: FASE58C_DISPLAY=$(FASE58C_DISPLAY)"; exit 1;; esac
	@echo "  FASE58C C  doomgeneric + $(REAL_WAD_PATH)"
	@echo "  QEMU     display=$(FASE58C_DISPLAY) (same as run-fase55d-doomgeneric-gui)"
	@echo "  LOG      serial -> $(FASE58C_DOOM_LOG)"
	@echo "  EXPECT   GUI: Doom | serial: DOOMGENERIC_WAD_LOAD_OK FIRST_FRAME FRAME_LOOP"
	@rm -f $(FASE58C_DOOM_LOG); \
	DISK=$$(mktemp /tmp/ir0-fase58c-doom.XXXXXX.img); \
	CFG=$$(mktemp /tmp/doom-frames-cfg.XXXXXX); \
	printf '%s\n%s\n' "$(DOOM_FRAMES)" "$(DOOM_FRAME_DUMP_EVERY)" > $$CFG; \
	dd if=/dev/zero of=$$DISK bs=1M count=200 status=none && \
	python3 scripts/inject_init_minix.py --format-large $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(FASE55E_DOOM_BIN) sbin/init && \
	python3 scripts/inject_init_minix.py $$DISK "$(REAL_WAD_PATH)" usr/share/doom/doom1.wad && \
	python3 scripts/inject_init_minix.py $$DISK $$CFG etc/doom-frames && \
	python3 scripts/verify_minix_rootfs.py $$DISK /sbin/init /usr/share/doom/doom1.wad /etc/doom-frames && \
	rm -f $$CFG; \
	if [ "$(FASE58C_DISPLAY)" = "sdl" ]; then \
		DISP="-display sdl2"; \
	else \
		DISP="-display gtk"; \
	fi; \
	$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial file:$(FASE58C_DOOM_LOG) \
		$$DISP -m 256M -no-reboot -net none; \
	rm -f $$DISK

# FASE58E — runit GUI/smoke targets live in root Makefile (avoid duplicate override).
# Legacy irinit path: load-userspace-irinit + run-irinit-interactive-gui below.

smoke-fase58e-ash-interactive: load-userspace-runit kernel-x64-userspace.iso
	@echo "  SMOKE   FASE58E ash interactive (headless + monitor sendkey)..."
	@chmod +x scripts/smoke_fase58e_ash_interactive.py
	@python3 scripts/smoke_fase58e_ash_interactive.py --log $(FASE58E_ASH_SMOKE_LOG) --timeout 90 --iso kernel-x64-userspace.iso
	@echo "  LOG     $(FASE58E_ASH_SMOKE_LOG)"
	@echo "  HINT    GUI manual: make run-fase58e-ash-gui && make check-fase58e-logs"

smoke-fase58l-busybox-coreutils: build-fase58l-busybox-smoke build-busybox-fase58-full kernel-x64-userspace.iso
	@echo "  SMOKE   FASE58L BusyBox full coreutils harness..."
	@strings $(FASE58L_SMOKE_BIN) 2>/dev/null | grep -q "FASE58L_HARNESS_ID" || \
		(echo "✗ $(FASE58L_SMOKE_BIN) is not FASE58L harness — run build-fase58l-busybox-smoke"; exit 1)
	@DISK=$$(mktemp /tmp/ir0-fase58l-disk.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=200 status=none && \
	python3 scripts/inject_init_minix.py --format-large $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(FASE58L_SMOKE_BIN) sbin/init && \
	chmod +x scripts/busybox_inject_manifest.sh && \
	FASE50_BUSYBOX_BIN=$(FASE50_BUSYBOX_BIN) scripts/busybox_inject_manifest.sh $$DISK $(FASE50_BUSYBOX_BIN) && \
	$(SMOKE_QEMU_RUN) --log $(FASE58L_SMOKE_LOG) --timeout 90 --done FASE58L_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK
	@if grep -q "FASE58L_OK" $(FASE58L_SMOKE_LOG) && \
	    grep -q "FASE58L_ECHO_OK" $(FASE58L_SMOKE_LOG) && \
	    grep -q "FASE58L_ECHO_PATH_OK" $(FASE58L_SMOKE_LOG) && \
	    grep -q "FASE58L_PWD_OK" $(FASE58L_SMOKE_LOG) && \
	    grep -q "FASE58L_LS_ROOT_OK" $(FASE58L_SMOKE_LOG) && \
	    grep -q "FASE58L_LS_PATH_OK" $(FASE58L_SMOKE_LOG) && \
	    grep -q "FASE58L_TOUCH_OK" $(FASE58L_SMOKE_LOG) && \
	    grep -q "FASE58L_CAT_OK" $(FASE58L_SMOKE_LOG) && \
	    grep -q "FASE58L_CAT_PATH_OK" $(FASE58L_SMOKE_LOG) && \
	    grep -q "FASE58L_UNAME_OK" $(FASE58L_SMOKE_LOG) && \
	    grep -q "BUSYBOX_MANIFEST_OK" $(FASE58L_SMOKE_LOG); then \
		echo "✓ smoke-fase58l-busybox-coreutils finished"; \
	else \
		echo "✗ smoke-fase58l-busybox-coreutils FAILED"; \
		grep -E 'FASE58L_|BUSYBOX_|KERNEL PANIC' $(FASE58L_SMOKE_LOG) | tail -30; \
		exit 1; \
	fi

# BUSY-2 ship gate: product config (fase58) + manifest inject + BUSYBOX_MANIFEST_OK
BUSYBOX_MANIFEST_SMOKE_LOG = /tmp/busybox-manifest-smoke.log
smoke-busybox-manifest: build-fase58l-busybox-smoke build-busybox-fase58-plus kernel-x64-userspace.iso
	@echo "  SMOKE   BUSY-2 product applet manifest..."
	@strings $(FASE58L_SMOKE_BIN) 2>/dev/null | grep -q "FASE58L_HARNESS_ID" || \
		(echo "✗ $(FASE58L_SMOKE_BIN) is not FASE58L harness — run build-fase58l-busybox-smoke"; exit 1)
	@DISK=$$(mktemp /tmp/ir0-busy-manifest-disk.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=200 status=none && \
	python3 scripts/inject_init_minix.py --format-large $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(FASE58L_SMOKE_BIN) sbin/init && \
	chmod +x scripts/busybox_inject_manifest.sh && \
	FASE50_BUSYBOX_BIN=$(FASE50_BUSYBOX_BIN) scripts/busybox_inject_manifest.sh $$DISK $(FASE50_BUSYBOX_BIN) && \
	$(SMOKE_QEMU_RUN) --log $(BUSYBOX_MANIFEST_SMOKE_LOG) --timeout 90 --done BUSYBOX_MANIFEST_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK
	@if grep -q "BUSYBOX_MANIFEST_OK" $(BUSYBOX_MANIFEST_SMOKE_LOG) && \
	    grep -q "FASE58L_OK" $(BUSYBOX_MANIFEST_SMOKE_LOG) && \
	    grep -q "FASE58L_ECHO_PATH_OK" $(BUSYBOX_MANIFEST_SMOKE_LOG) && \
	    grep -q "FASE58L_LS_PATH_OK" $(BUSYBOX_MANIFEST_SMOKE_LOG) && \
	    grep -q "FASE58L_CAT_PATH_OK" $(BUSYBOX_MANIFEST_SMOKE_LOG); then \
		echo "✓ smoke-busybox-manifest finished"; \
	else \
		echo "✗ smoke-busybox-manifest FAILED"; \
		grep -E 'FASE58L_|BUSYBOX_|KERNEL PANIC' $(BUSYBOX_MANIFEST_SMOKE_LOG) | tail -30; \
		exit 1; \
	fi

check-fase58c-logs:
	@echo "=== FASE58C A (boot) ==="
	@if [ -f "$(FASE58C_BOOT_LOG)" ]; then \
		grep -E 'FB_BOOT_DIRECT_DRAW_OK|\[FB_BOOT\]' "$(FASE58C_BOOT_LOG)" || echo "(no boot tags)"; \
	else echo "missing $(FASE58C_BOOT_LOG)"; fi
	@echo "=== FASE58C B (fbdev) ==="
	@if [ -f "$(FASE58C_FBDEV_LOG)" ]; then \
		grep -E 'DEVFB0_DRAW_OK|FASE58C_OK|\[FASE58C\]\[FAIL\]' "$(FASE58C_FBDEV_LOG)" || echo "(no fbdev tags)"; \
	else echo "missing $(FASE58C_FBDEV_LOG)"; fi
	@echo "=== FASE58C C (doom) ==="
	@if [ -f "$(FASE58C_DOOM_LOG)" ]; then \
		grep -E 'DOOMGENERIC_WAD_LOAD_OK|DOOMGENERIC_FIRST_FRAME_OK|DOOMGENERIC_FRAME_LOOP_OK|DOOMGENERIC_FRAMEBUFFER_VISIBLE' "$(FASE58C_DOOM_LOG)" || echo "(no doom tags)"; \
	else echo "missing $(FASE58C_DOOM_LOG)"; fi

run-irinit-interactive-gui: build-irinit build-busybox-fase50-min build-tcc-fase52 kernel-x64-userspace.iso
	@echo "  RUN     irinit GUI interactive (BusyBox ash on /dev/console)"
	@echo "  LOG     serial -> $(IRINIT_GUI_LOG)"
	@echo "  HINT    click QEMU window to capture keyboard; Ctrl+C to stop"
	@echo "  HINT    type ls, pwd, echo hi, cat /proc/mounts manually"
	@rm -f $(IRINIT_GUI_LOG); \
	DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=200 status=none && \
	python3 scripts/inject_init_minix.py --format-large $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(IRINIT_BIN) sbin/init && \
	python3 scripts/inject_init_minix.py $$DISK $(IRINIT_BIN) sbin/irinit && \
	python3 scripts/inject_init_minix.py $$DISK $(FASE50_BUSYBOX_BIN) bin/busybox && \
	python3 scripts/inject_init_minix.py $$DISK $(FASE50_BUSYBOX_BIN) bin/sh && \
	find $(FASE52_TCC_STAGE) -type f 2>/dev/null | sort | while read -r f; do \
		rel="$${f#$(FASE52_TCC_STAGE)/}"; \
		python3 scripts/inject_init_minix.py $$DISK "$$f" "$$rel"; \
	done; \
	if [ -n "$(REAL_WAD_PATH)" ] && [ -f "$(REAL_WAD_PATH)" ]; then \
		$(MAKE) -s build-fase55e-doom-interactive; \
		CFG=$$(mktemp /tmp/doom-frames-cfg.XXXXXX); \
		printf '0\n0\n' > $$CFG; \
		python3 scripts/inject_init_minix.py $$DISK $(FASE55E_DOOM_BIN) bin/doomgeneric && \
		python3 scripts/inject_init_minix.py $$DISK "$(REAL_WAD_PATH)" usr/share/doom/doom1.wad && \
		python3 scripts/inject_init_minix.py $$DISK $$CFG etc/doom-frames && \
		rm -f $$CFG; \
	fi; \
	python3 scripts/verify_minix_rootfs.py $$DISK /sbin/irinit /bin/sh /bin/busybox; \
	if [ "$(IRINIT_DISPLAY)" = "sdl" ]; then \
		DISP="-display sdl2"; \
	else \
		DISP="-display gtk"; \
	fi; \
	$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial file:$(IRINIT_GUI_LOG) \
		$$DISP -m 256M -no-reboot -net none; \
	rm -f $$DISK

smoke-current-fase54b: kernel-x64.bin arch-guard smoke-fase54b-input
	@echo "FAST_ITERATION_GATES_OK"

smoke-regression-light: kernel-x64.bin arch-guard smoke-fase53b-posix-pseudofs smoke-fase54a-fbdev smoke-fase54b-input
	@echo "FAST_ITERATION_GATES_OK"

# Serial smokes: each target overwrites setup/pid1/init — do not run in parallel.
smoke-regression-light-fast: kernel-x64.bin arch-guard
	@$(MAKE) -s smoke-fase54c-input-deterministic
	@$(MAKE) -s smoke-fase55b-doom-stub
	@$(MAKE) -s smoke-fase55c-timing-input
	@echo "FAST_ITERATION_GATES_OK"

smoke-regression-full: kernel-x64.bin arch-guard build-matrix-min smoke-fase50-busybox smoke-fase51-shell smoke-fase52-tcc smoke-fase53a-fs-dev smoke-fase53b-posix-pseudofs smoke-fase54a-fbdev smoke-fase54b-input smoke-fase55a-doom-prereq smoke-runit-boot smoke-runit-ash-interactive smoke-fase58e-ash-interactive smoke-fase58l-busybox-coreutils
	@$(MAKE) -s -C tests/host run
	@if [ -n "$(REAL_WAD_PATH)" ] && [ -f "$(REAL_WAD_PATH)" ]; then \
		$(MAKE) -s smoke-fase55d-doomgeneric REAL_WAD_PATH="$(REAL_WAD_PATH)"; \
	else \
		echo "  SKIP    smoke-fase55d-doomgeneric (set REAL_WAD_PATH for real WAD CI)"; \
	fi
	@echo "FULL_REGRESSION_ON_CLOSE_OK"

smoke-fase50-programs: build-init-fase50-programs build-fase50-hello build-busybox-fase50-min kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) create-disk; \
	fi
	@echo "  SMOKE   FASE50 real programs..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	python3 scripts/inject_init_minix.py $$DISK $(FASE50_HELLO_BIN) bin/hello-world; \
	python3 scripts/inject_init_minix.py $$DISK $(FASE50_BUSYBOX_BIN) bin/busybox; \
	$(SMOKE_QEMU_RUN) --log $(FASE50_PROGRAMS_LOG) --timeout 180 --done USERSPACE_BOOTSTRAP_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK;
	@if grep -q "PROGRAM=hello-world" $(FASE50_PROGRAMS_LOG) && \
	    grep -q "PROGRAM=busybox echo" $(FASE50_PROGRAMS_LOG) && \
	    grep -q "PROGRAM=busybox cat" $(FASE50_PROGRAMS_LOG) && \
	    grep -q "PROGRAM=busybox ls" $(FASE50_PROGRAMS_LOG) && \
	    grep -q "USERSPACE_BOOTSTRAP_OK" $(FASE50_PROGRAMS_LOG); then \
		echo "✓ smoke-fase50-programs finished"; \
	else \
		echo "✗ smoke-fase50-programs FAILED"; \
		exit 1; \
	fi

# Remove Init binary from virtual disk
# Usage: make remove-init [filesystem] [disk_image]
# Defaults: filesystem=auto-detect, disk_image=disk.img
# Supported filesystems: minix, fat16, fat32, ext4
# Note: Requires root privileges (mounts filesystem)
# Examples:
#   sudo make remove-init                  # Auto-detect, use disk.img
#   sudo make remove-init fat32            # Use fat32.img
#   sudo make remove-init fat32 fat32.img  # Explicit filesystem and disk
#   make remove-init hints                 # Show help
remove-init:
	@ARGS="$(filter-out $@,$(MAKECMDGOALS))"; \
	if [ -z "$$ARGS" ]; then \
		./scripts/remove_init.sh; \
	elif [ "$$ARGS" = "hints" ] || [ "$$ARGS" = "help" ]; then \
		./scripts/remove_init.sh --help; \
	else \
		./scripts/remove_init.sh $$ARGS; \
	fi

# CLEAN

