# SPDX-License-Identifier: GPL-3.0-only
# QA / smoke / test / gate targets — not part of default `make help`.
# Invoke via:  scripts/ir0-qa.sh <target>
# Or:          make IR0_INCLUDE_QA=1 <target>
# Legacy phase smokes: IR0_LEGACY_SMOKE=1 scripts/ir0-qa.sh smoke-fase50-busybox

ifndef IR0_INCLUDE_QA
$(error scripts/make/qa.mk: set IR0_INCLUDE_QA=1 or use scripts/ir0-qa.sh)
endif

export IR0_INCLUDE_QA := 1

# Kernel con tests in-kernel (solo al hacer make tests / kernel-tests)
# config.h documents IR0_KERNEL_TESTS; Makefile ensures it for this target
kernel-x64-test.bin: CFLAGS += -DIR0_KERNEL_TESTS=1
kernel-x64-test.bin: $(ALL_OBJS_TEST) arch/x86-64/linker.ld
	@echo "  LD      $@ (with in-kernel tests)"
	@$(LD) $(LDFLAGS) -o $@ $(ALL_OBJS_TEST)
	@echo "✓ Kernel (test) linked: $@"

kernel-x64-test.iso: kernel-x64-test.bin arch/x86-64/grub.cfg
	@echo "  ISO     $@"
	@rm -rf iso_test
	@mkdir -p iso_test/boot/grub
	@cp arch/x86-64/grub.cfg iso_test/boot/grub/
	@cp kernel-x64-test.bin iso_test/boot/kernel-x64.bin
	@grub-mkrescue -o $@ iso_test
	@rm -rf iso_test
	@echo "✓ ISO (test) created: $@"

# Userspace init smoke binaries (copied to /sbin/init via load-init)
INIT_SMOKE_SRC = setup/pid1/init_smoke.c
INIT_MUSL_SRC  = setup/pid1/init_musl.c
MUSL_ARCH_PRCTL_SMOKE_SRC = setup/pid1/musl_arch_prctl_smoke.c
MUSL_PTHREAD_SMOKE_SRC = setup/pid1/musl_pthread_smoke.c
SU_SETUID_SMOKE_SRC = setup/pid1/su_setuid_smoke.c
INIT_MINIMAL_SRC = setup/pid1/init_minimal.c
INIT_SEGV_SMOKE_SRC = setup/pid1/init_segv_smoke.c
INIT_HEAP_SMOKE_SRC = setup/pid1/init_heap_smoke.c
INIT_FAT16_SMOKE_SRC = setup/pid1/init_fat16_smoke.c
INIT_MMAP_SMOKE_SRC = setup/pid1/init_mmap_smoke.c
INIT_STACK_HEAP_ISO_SRC = setup/pid1/init_stack_heap_iso_smoke.c
INIT_FORK_MEM_SMOKE_SRC = setup/pid1/init_fork_mem_smoke.c
INIT_FASE41_RECLAIM_SRC = setup/pid1/init_fase41_reclaim.c
INIT_FASE42_PT_RECLAIM_SRC = setup/pid1/init_fase42_pt_reclaim.c
INIT_FASE42_EXEC_STORM_SRC = setup/pid1/init_fase42_exec_storm.c
INIT_FASE42_FORK_EXIT_STORM_SRC = setup/pid1/init_fase42_fork_exit_storm.c
INIT_FASE43_FORK_EXIT_STORM_SRC = setup/pid1/init_fase43_fork_exit_storm.c
INIT_FASE43_FORK_WAIT_STORM_SRC = setup/pid1/init_fase43_fork_wait_storm.c
INIT_FASE43_EXEC_LOOP_SRC = setup/pid1/init_fase43_exec_loop.c
INIT_FASE44_FORK_WAIT_DRAIN_SRC = setup/pid1/init_fase44_fork_wait_drain.c
INIT_FASE44_EXEC_DRAIN_SRC = setup/pid1/init_fase44_exec_drain.c
INIT_FASE44_INIT_EXIT_DRAIN_SRC = setup/pid1/init_fase44_init_exit_drain.c
INIT_FASE45_FORK_ROLLBACK_STORM_SRC = setup/pid1/init_fase45_fork_rollback_storm.c
INIT_FASE45_FORK_MEM_TOUCH_SRC = setup/pid1/init_fase45_fork_mem_touch.c
INIT_FASE46_FORK_NO_RECURSE_SRC = setup/pid1/init_fase46_fork_no_recursion.c
INIT_FASE46_FORK_HEAP_SRC = setup/pid1/init_fase46_fork_heap.c
INIT_FASE48_IPC_SRC = setup/pid1/init_fase48_ipc.c
INIT_FASE49_PIPE_SRC = setup/pid1/init_fase49_pipe.c
INIT_FASE50_BUSYBOX_SRC = setup/pid1/init_fase50_busybox.c
INIT_FASE50_EXEC_ONLY_SRC = setup/pid1/init_fase50_exec_only.c
INIT_FASE51_SHELL_SRC = setup/pid1/init_fase51_shell.c
INIT_FASE52_TCC_SRC = setup/pid1/init_fase52_tcc.c
FASE52_HARNESS_BIN = setup/pid1/fase52_harness
FASE55D_SMOKE_BIN = setup/doom/doomgeneric_smoke
RUNIT_STAGE_BIN = setup/runit/stage-bin
INIT_FASE53A_FS_DEV_SRC = setup/pid1/init_fase53a_fs_dev.c
INIT_FASE53B_POSIX_PSEUDOFS_SRC = setup/pid1/init_fase53b_posix_pseudofs.c
INIT_HEART_SMOKE_SRC = setup/pid1/init_heart_smoke.c
INIT_FASE54A_FBDEV_SRC = setup/pid1/init_fase54a_fbdev.c
INIT_FASE54B_INPUT_SRC = setup/pid1/init_fase54b_input.c
INIT_FASE54C_INPUT_DET_SRC = setup/pid1/init_fase54c_input_deterministic.c
INIT_FASE55A_DOOM_PREREQ_SRC = setup/pid1/init_fase55a_doom_prereq.c
INIT_FASE55B_DOOM_STUB_SRC = setup/doom/doomgeneric_ir0_stub.c
INIT_FASE55C_TIMING_INPUT_SRC = setup/doom/doomgeneric_ir0_stub.c
INIT_FASE55D_DOOMGENERIC_SRC = setup/doom/doomgeneric_ir0.c
INIT_FASE58C_BOOT_HALT_SRC = setup/pid1/init_fase58c_boot_halt.c
INIT_FASE58C_FBDEV_SRC = setup/pid1/init_fase58c_fbdev.c
FASE58C_BOOT_BIN = setup/pid1/fase58c_boot_halt
FASE58C_FBDEV_BIN = setup/pid1/fase58c_fbdev
FASE58C_DISPLAY ?= gtk
FASE58C_BOOT_LOG = /tmp/fase58c-boot-gui.log
FASE58C_FBDEV_LOG = /tmp/fase58c-fbdev-gui.log
FASE58C_DOOM_LOG = /tmp/fase58c-doom-gui.log
FASE58E_ASH_LOG = /tmp/fase58e-ash-gui.log
FASE58E_DISPLAY ?= gtk
FASE58E_ASH_SMOKE_LOG = /tmp/fase58e-ash-smoke.log
INIT_FASE50_PROGRAMS_SRC = setup/pid1/init_fase50_programs.c
FASE48_CAT_SRC = setup/pid1/fase48_cat.c
FASE48_ECHO_SRC = setup/pid1/fase48_echo.c
FASE48_BUSYBOX_SRC = setup/pid1/fase48_busybox.c
FASE50_HELLO_SRC = setup/pid1/fase50_hello.c
FASE48_CAT_BIN = setup/pid1/fase48_cat
FASE48_ECHO_BIN = setup/pid1/fase48_echo
FASE48_BUSYBOX_BIN = setup/pid1/fase48_busybox
FASE50_HELLO_BIN = setup/pid1/fase50_hello
FASE50_BUSYBOX_BIN = setup/pid1/fase50_busybox_real
FASE50_BUSYBOX_CFG = setup/busybox/fase58_busybox.config
FASE58_BUSYBOX_CFG = setup/busybox/fase58_busybox.config
FASE58_FULL_BUSYBOX_CFG = setup/busybox/fase58_full.config
FASE58L_SMOKE_SRC = setup/pid1/fase58l_busybox_smoke.c
FASE58L_SMOKE_BIN = setup/pid1/fase58l_busybox_smoke
FASE58L_SMOKE_LOG = /tmp/fase58l-busybox-smoke.log
FASE41_TRUE_SRC = setup/pid1/fase41_true.c
SH_SMOKE_SRC     = setup/pid1/sh_smoke.c
SEGV_SMOKE_SRC   = setup/pid1/userspace_segv.c
INIT_SMOKE_BIN   = setup/pid1/init
MUSL_ARCH_PRCTL_BIN = setup/pid1/musl_arch_prctl_smoke
MUSL_PTHREAD_SMOKE_BIN = setup/pid1/musl_pthread_smoke
SU_SETUID_SMOKE_BIN = setup/pid1/su_setuid_smoke
SH_SMOKE_BIN     = setup/pid1/sh_smoke
SEGV_SMOKE_BIN   = setup/pid1/userspace_segv
FASE41_TRUE_BIN  = setup/pid1/f41true
HEAP_SMOKE_LOG   = /tmp/userspace-heap-smoke.log
MMAP_SMOKE_LOG   = /tmp/userspace-mmap-smoke.log
ISO_SMOKE_LOG    = /tmp/userspace-stack-heap-iso.log
FORK_MEM_SMOKE_LOG = /tmp/userspace-fork-mem-smoke.log
FAT16_SMOKE_IMG  = build/fat16_smoke.img
FAT16_SMOKE_LOG  = /tmp/fat16-smoke.log
FASE41_RECLAIM_LOG = /tmp/userspace-fase41-reclaim.log
FASE42_PT_RECLAIM_LOG = /tmp/userspace-fase42-pt-reclaim.log
FASE42_EXEC_STORM_LOG = /tmp/userspace-fase42-exec-storm.log
FASE42_FORK_EXIT_STORM_LOG = /tmp/userspace-fase42-fork-exit-storm.log
FASE43_FORK_EXIT_STORM_LOG = /tmp/userspace-fase43-fork-exit-storm.log
FASE43_FORK_WAIT_STORM_LOG = /tmp/userspace-fase43-fork-wait-storm.log
FASE43_EXEC_LOOP_LOG = /tmp/userspace-fase43-exec-loop.log
FASE44_FORK_WAIT_DRAIN_LOG = /tmp/userspace-fase44-fork-wait-drain.log
FASE44_EXEC_DRAIN_LOG = /tmp/userspace-fase44-exec-drain.log
FASE44_INIT_EXIT_DRAIN_LOG = /tmp/userspace-fase44-init-exit-drain.log
FASE45_FORK_ROLLBACK_STORM_LOG = /tmp/userspace-fase45-fork-rollback-storm.log
FASE45_FORK_MEM_TOUCH_LOG = /tmp/userspace-fase45-fork-mem-touch.log
FASE46_FORK_NO_RECURSE_LOG = /tmp/userspace-fase46-fork-no-recursion.log
FASE46_FORK_HEAP_LOG = /tmp/userspace-fase46-fork-heap.log
FASE48_IPC_LOG = /tmp/userspace-fase48-ipc.log
FASE49_PIPE_LOG = /tmp/userspace-fase49-pipe.log
FASE50_BUSYBOX_LOG = /tmp/userspace-fase50-busybox.log
FASE50_EXEC_ONLY_LOG = /tmp/userspace-fase50-exec-only.log
FASE51_SHELL_LOG = /tmp/userspace-fase51-shell.log
FASE52_TCC_LOG = /tmp/userspace-fase52-tcc.log
# Bisect lazy vs eager MM in legacy smokes: KERNEL_USERSPACE_ISO=kernel-x64-userspace-eager.iso
KERNEL_USERSPACE_ISO ?= kernel-x64-userspace.iso
FASE53A_FS_DEV_LOG = /tmp/userspace-fase53a-fs-dev.log
FASE53B_POSIX_PSEUDOFS_LOG = /tmp/userspace-fase53b-posix-pseudofs.log
HEART_SMOKE_LOG = /tmp/userspace-heart.log
FASE54A_FBDEV_LOG = /tmp/userspace-fase54a-fbdev.log
FASE54B_INPUT_LOG = /tmp/userspace-fase54b-input.log
FASE54C_INPUT_DET_LOG = /tmp/userspace-fase54c-input-deterministic.log
FASE55A_DOOM_PREREQ_LOG = /tmp/userspace-fase55a-doom-prereq.log
FASE55B_DOOM_STUB_LOG = /tmp/userspace-fase55b-doom-stub.log
FASE55C_TIMING_INPUT_LOG = /tmp/userspace-fase55c-timing-input.log
FASE55D_DOOMGENERIC_LOG = /tmp/userspace-fase55d-doomgeneric.log
MUSL_ARCH_PRCTL_LOG = /tmp/userspace-musl-arch-prctl.log
MUSL_PTHREAD_SMOKE_LOG = /tmp/userspace-musl-pthread.log
SU_SETUID_SMOKE_LOG = /tmp/userspace-su-setuid.log
FASE55E_DOOM_BIN = setup/pid1/fase55e_doom_interactive
FASE55E_DOOM_GUI_LOG = /tmp/fase55e-doomgeneric-gui.log
IRINIT_SRC = setup/pid1/irinit.c
IRINIT_BIN = setup/pid1/sbin/irinit
RUNIT_VERSION = 2.3.1
RUNIT_SRC_DIR = setup/third-party/runit-$(RUNIT_VERSION)
RUNIT_BIN_DIR = setup/runit/bin
RUNIT_INIT_BIN = $(RUNIT_BIN_DIR)/runit-init
RUNIT_SMOKE_LOG = /tmp/runit-boot-smoke.log
RUNIT_ASH_SMOKE_LOG = /tmp/runit-ash-smoke.log
IRINIT_GUI_LOG = /tmp/userspace-irinit-gui.log
IRINIT_DISPLAY ?= gtk
DOOM_FRAMES ?= 0
DOOM_FRAME_DUMP_EVERY ?= 0
DOOM_DISPLAY ?= gtk
REAL_WAD_PATH ?= /home/ivanr013/Escritorio/universal-doom/DOOM1.WAD
FASE52_TCC_STAGE = setup/pid1/fase52_staging
FASE50_PROGRAMS_LOG = /tmp/userspace-fase50-programs.log
# Serial-log autokill: scripts/smoke_autokill.py (default max 180s; heavy smokes use --profile 90–120s).
SMOKE_QEMU_RUN = bash scripts/smoke_qemu_run.sh
MUSL_CC ?= $(shell command -v x86_64-linux-musl-gcc 2>/dev/null || command -v musl-gcc 2>/dev/null)
BUSYBOX_SRC ?= $(KERNEL_ROOT)/setup/third-party/busybox-1.36.1
TCC_SRC ?= /tmp/tinycc-fase52

build-init-smoke:
	@echo "  INIT    Building nostdlib ring-3 smoke ($(INIT_SMOKE_BIN))"
	@$(CC) -nostdlib -static -Os -fno-stack-protector -Wl,-e,_start -Wl,-s,-z,noexecstack \
		-o $(INIT_SMOKE_BIN) $(INIT_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-smoke OK ($(shell stat -c%s $(INIT_SMOKE_BIN) 2>/dev/null || stat -f%z $(INIT_SMOKE_BIN)) bytes)"

build-init-musl:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building musl static smoke with $(MUSL_CC)"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_MUSL_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-musl OK"

# Minimal musl static arch_prctl(ARCH_SET_FS) gate (F probe prerequisite).
build-musl-arch-prctl-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  MUSL    Building arch_prctl smoke ($(MUSL_ARCH_PRCTL_BIN))"
	@$(MUSL_CC) -static -Os -o $(MUSL_ARCH_PRCTL_BIN) $(MUSL_ARCH_PRCTL_SMOKE_SRC)
	@file $(MUSL_ARCH_PRCTL_BIN) | grep -q ELF
	@strings $(MUSL_ARCH_PRCTL_BIN) 2>/dev/null | grep -q "MUSL_ARCH_PRCTL_OK" || \
		(echo "✗ arch_prctl smoke missing MUSL_ARCH_PRCTL_OK string"; exit 1)
	@echo "✓ build-musl-arch-prctl-smoke OK"

build-musl-pthread-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  MUSL    Building pthread smoke ($(MUSL_PTHREAD_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(MUSL_PTHREAD_SMOKE_BIN) $(MUSL_PTHREAD_SMOKE_SRC)
	@file $(MUSL_PTHREAD_SMOKE_BIN) | grep -q ELF
	@strings $(MUSL_PTHREAD_SMOKE_BIN) 2>/dev/null | grep -q "MUSL_PTHREAD_OK" || \
		(echo "✗ pthread smoke missing MUSL_PTHREAD_OK string"; exit 1)
	@echo "✓ build-musl-pthread-smoke OK"

build-su-setuid-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  MUSL    Building setuid exec smoke ($(SU_SETUID_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(SU_SETUID_SMOKE_BIN) $(SU_SETUID_SMOKE_SRC)
	@file $(SU_SETUID_SMOKE_BIN) | grep -q ELF
	@strings $(SU_SETUID_SMOKE_BIN) 2>/dev/null | grep -q "SU_SETUID_OK" || \
		(echo "✗ setuid smoke missing SU_SETUID_OK string"; exit 1)
	@echo "✓ build-su-setuid-smoke OK"

# Minimal PID 1: fork/execve/wait4 (musl); needs /bin/sh on disk for oleada 2 smoke.
build-init-minimal:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building musl minimal PID1 ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_MINIMAL_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-minimal OK"

# Minimal /bin/sh stub for fork+execve smoke (musl static).
build-sh-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  SH      Building musl /bin/sh stub ($(SH_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(SH_SMOKE_BIN) $(SH_SMOKE_SRC)
	@file $(SH_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-sh-smoke OK"

build-init-segv-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building userspace segv PID1 smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_SEGV_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-segv-smoke OK"

build-userspace-segv:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  SEGv    Building /bin/userspace_segv ($(SEGV_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(SEGV_SMOKE_BIN) $(SEGV_SMOKE_SRC)
	@file $(SEGV_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-userspace-segv OK"

build-init-heap-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building userspace heap smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_HEAP_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-heap-smoke OK"

build-init-fat16-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FAT16 mount/read smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FAT16_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fat16-smoke OK"

build-init-mmap-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building userspace mmap smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_MMAP_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-mmap-smoke OK"

build-init-stack-heap-iso-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building stack/heap isolation smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_STACK_HEAP_ISO_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-stack-heap-iso-smoke OK"

build-init-fork-mem-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building fork memory smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FORK_MEM_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fork-mem-smoke OK"

build-fase41-true:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  TRUE    Building FASE41 /bin/f41true ($(FASE41_TRUE_BIN))"
	@$(MUSL_CC) -static -Os -o $(FASE41_TRUE_BIN) $(FASE41_TRUE_SRC)
	@file $(FASE41_TRUE_BIN) | grep -q ELF
	@echo "✓ build-fase41-true OK"

build-init-fase41-reclaim:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE41 reclaim smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE41_RECLAIM_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase41-reclaim OK"

build-init-fase42-pt-reclaim:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE42 page-table reclaim smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE42_PT_RECLAIM_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase42-pt-reclaim OK"

build-init-fase42-exec-storm:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE42 exec storm smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE42_EXEC_STORM_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase42-exec-storm OK"

build-init-fase42-fork-exit-storm:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE42 fork+exit storm smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE42_FORK_EXIT_STORM_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase42-fork-exit-storm OK"

build-init-fase43-fork-exit-storm:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE43 fork+exit storm smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE43_FORK_EXIT_STORM_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase43-fork-exit-storm OK"

build-init-fase43-fork-wait-storm:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE43 fork+wait storm smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE43_FORK_WAIT_STORM_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase43-fork-wait-storm OK"

build-init-fase43-exec-loop:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE43 exec loop smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE43_EXEC_LOOP_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase43-exec-loop OK"

build-init-fase44-fork-wait-drain:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE44 fork-wait-drain smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE44_FORK_WAIT_DRAIN_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase44-fork-wait-drain OK"

build-init-fase44-exec-drain:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE44 exec-drain smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE44_EXEC_DRAIN_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase44-exec-drain OK"

build-init-fase44-init-exit-drain:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE44 init-exit-drain smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE44_INIT_EXIT_DRAIN_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase44-init-exit-drain OK"

build-init-fase45-fork-rollback-storm:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE45 fork rollback storm ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE45_FORK_ROLLBACK_STORM_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase45-fork-rollback-storm OK"

build-init-fase45-fork-mem-touch:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE45 fork mem touch ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE45_FORK_MEM_TOUCH_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase45-fork-mem-touch OK"

build-init-fase46-fork-no-recursion:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE46 fork-no-recursion ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE46_FORK_NO_RECURSE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase46-fork-no-recursion OK"

build-init-fase46-fork-heap:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE46 fork+heap ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE46_FORK_HEAP_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase46-fork-heap OK"

build-fase48-ipc-bins:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  FASE48  Building /bin/cat /bin/echo /bin/busybox"
	@$(MUSL_CC) -static -Os -o $(FASE48_CAT_BIN) $(FASE48_CAT_SRC)
	@$(MUSL_CC) -static -Os -o $(FASE48_ECHO_BIN) $(FASE48_ECHO_SRC)
	@$(MUSL_CC) -static -Os -o $(FASE48_BUSYBOX_BIN) $(FASE48_BUSYBOX_SRC)
	@file $(FASE48_CAT_BIN) $(FASE48_ECHO_BIN) $(FASE48_BUSYBOX_BIN) | grep -q ELF
	@echo "✓ build-fase48-ipc-bins OK"

build-init-fase48-ipc:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE48 IPC smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE48_IPC_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase48-ipc OK"

build-init-fase49-pipe:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE49 pipe smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE49_PIPE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase49-pipe OK"

build-busybox-fase50-min:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@if [ ! -d "$(BUSYBOX_SRC)" ] || [ ! -f "$(BUSYBOX_SRC)/Makefile" ]; then \
		echo "✗ BusyBox source missing at BUSYBOX_SRC=$(BUSYBOX_SRC)"; \
		echo "  Expected vendored tree: setup/third-party/busybox-1.36.1"; \
		echo "  Or override: make ... BUSYBOX_SRC=/path/to/busybox-<version>"; \
		exit 1; \
	fi
	@if [ ! -f "$(FASE50_BUSYBOX_CFG)" ]; then \
		echo "✗ Missing config fragment $(FASE50_BUSYBOX_CFG)"; \
		exit 1; \
	fi
	@echo "  FASE50  Building ash+coreutils static BusyBox from $(BUSYBOX_SRC)"
	@chmod +x scripts/busybox_apply_fragment.sh
	@scripts/busybox_apply_fragment.sh "$(BUSYBOX_SRC)" "$(FASE50_BUSYBOX_CFG)"
	@$(MAKE) -C "$(BUSYBOX_SRC)" CC="$(MUSL_CC)" CFLAGS="-fno-pie" LDFLAGS="-no-pie" -j$$(nproc)
	@cp -f "$(BUSYBOX_SRC)/busybox" "$(FASE50_BUSYBOX_BIN)"
	@file "$(FASE50_BUSYBOX_BIN)" | grep -q ELF
	@echo "✓ build-busybox-fase50-min OK"

build-busybox-fase58-plus:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@if [ ! -d "$(BUSYBOX_SRC)" ] || [ ! -f "$(BUSYBOX_SRC)/Makefile" ]; then \
		echo "✗ BusyBox source missing at BUSYBOX_SRC=$(BUSYBOX_SRC)"; \
		exit 1; \
	fi
	@if [ ! -f "$(FASE58_BUSYBOX_CFG)" ]; then \
		echo "✗ Missing config fragment $(FASE58_BUSYBOX_CFG)"; \
		exit 1; \
	fi
	@echo "  FASE58  Building ash+coreutils BusyBox from $(BUSYBOX_SRC)"
	@chmod +x scripts/busybox_apply_fragment.sh
	@scripts/busybox_apply_fragment.sh "$(BUSYBOX_SRC)" "$(FASE58_BUSYBOX_CFG)"
	@$(MAKE) -C "$(BUSYBOX_SRC)" CC="$(MUSL_CC)" CFLAGS="-fno-pie" LDFLAGS="-no-pie" -j$$(nproc)
	@cp -f "$(BUSYBOX_SRC)/busybox" "$(FASE50_BUSYBOX_BIN)"
	@file "$(FASE50_BUSYBOX_BIN)" | grep -q ELF
	@echo "✓ build-busybox-fase58-plus OK (installed to $(FASE50_BUSYBOX_BIN))"

build-busybox-fase58-full:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@if [ ! -d "$(BUSYBOX_SRC)" ] || [ ! -f "$(BUSYBOX_SRC)/Makefile" ]; then \
		echo "✗ BusyBox source missing at BUSYBOX_SRC=$(BUSYBOX_SRC)"; \
		exit 1; \
	fi
	@if [ ! -f "$(FASE58_FULL_BUSYBOX_CFG)" ]; then \
		echo "✗ Missing config fragment $(FASE58_FULL_BUSYBOX_CFG)"; \
		exit 1; \
	fi
	@echo "  FASE58L Building full applets BusyBox from $(BUSYBOX_SRC)"
	@chmod +x scripts/busybox_apply_fragment.sh
	@scripts/busybox_apply_fragment.sh "$(BUSYBOX_SRC)" "$(FASE58_FULL_BUSYBOX_CFG)"
	@$(MAKE) -C "$(BUSYBOX_SRC)" CC="$(MUSL_CC)" CFLAGS="-fno-pie" LDFLAGS="-no-pie" -j$$(nproc)
	@cp -f "$(BUSYBOX_SRC)/busybox" "$(FASE50_BUSYBOX_BIN)"
	@file "$(FASE50_BUSYBOX_BIN)" | grep -q ELF
	@echo "✓ build-busybox-fase58-full OK (installed to $(FASE50_BUSYBOX_BIN))"

build-fase58l-busybox-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE58L BusyBox smoke ($(FASE58L_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(FASE58L_SMOKE_BIN) $(FASE58L_SMOKE_SRC)
	@file $(FASE58L_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-fase58l-busybox-smoke OK"

build-fase50-hello:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  FASE50  Building hello-world ($(FASE50_HELLO_BIN))"
	@$(MUSL_CC) -static -Os -o $(FASE50_HELLO_BIN) $(FASE50_HELLO_SRC)
	@file $(FASE50_HELLO_BIN) | grep -q ELF
	@echo "✓ build-fase50-hello OK"

build-init-fase50-busybox:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE50 BusyBox smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE50_BUSYBOX_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase50-busybox OK"

build-init-fase50-exec-only:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE50 EXEC-only smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE50_EXEC_ONLY_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase50-exec-only OK"

build-init-fase51-shell:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE51 shell smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE51_SHELL_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase51-shell OK"

build-irinit:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building irinit PID1 ($(IRINIT_BIN))"
	@mkdir -p setup/pid1/sbin
	@$(MUSL_CC) -static -Os -o $(IRINIT_BIN) $(IRINIT_SRC)
	@file $(IRINIT_BIN) | grep -q ELF
	@echo "✓ build-irinit OK"

build-runit:
	@chmod +x setup/runit/build-runit.sh
	@./setup/runit/build-runit.sh

load-userspace-runit: build-runit build-busybox-fase50-min
	@DISK=$${DISK:-disk.img}; \
	echo "  DISK    Preparing $$DISK (200M MINIX) for runit..."; \
	dd if=/dev/zero of=$$DISK bs=1M count=200 status=none; \
	python3 scripts/inject_init_minix.py --format-large $$DISK; \
	chmod +x setup/runit/install-to-disk.sh; \
	FASE50_BUSYBOX_BIN=$(FASE50_BUSYBOX_BIN) ./setup/runit/install-to-disk.sh $$DISK
	@echo "✓ load-userspace-runit OK (runit-init → runsvdir → console + logger)"

smoke-runit-boot: load-userspace-runit kernel-x64-userspace.iso
	@echo "  SMOKE   runit PID1 boot (console + logger)..."
	@DISK=$$(mktemp /tmp/ir0-runit-smoke.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	$(SMOKE_QEMU_RUN) --log $(RUNIT_SMOKE_LOG) --timeout 50 --stale-sec 18 \
		--done RUNSV_CONSOLE_START --done RUNSV_LOGGER_START -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK
	@if grep -q "RUNIT_STAGE1_OK" $(RUNIT_SMOKE_LOG) && \
	    grep -q "RUNIT_STAGE2_OK" $(RUNIT_SMOKE_LOG) && \
	    grep -q "RUNSV_CONSOLE_START" $(RUNIT_SMOKE_LOG) && \
	    grep -q "RUNSV_LOGGER_START" $(RUNIT_SMOKE_LOG); then \
		echo "✓ smoke-runit-boot passed (2 services)"; \
	else \
		echo "✗ smoke-runit-boot FAILED"; \
		grep -E 'RUNIT_|RUNSV_|KERNEL PANIC|panic' $(RUNIT_SMOKE_LOG) | tail -25; \
		exit 1; \
	fi

# MM vertical slice: lazy alloc (brk + anon mmap) + fork COW (FASE40 A–F).
smoke-mm-cow-lazy: kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) -s create-disk; \
	fi
	@echo "  SMOKE   MM lazy alloc + fork COW (heap + mmap + FASE40, lazy kernel)..."
	@$(MAKE) -s build-init-heap-smoke
	@DISK=$$(mktemp /tmp/ir0-mm-cow-lazy.XXXXXX.img); \
	truncate -s 200M $$DISK; \
	python3 scripts/inject_init_minix.py --format-large $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	rm -f $(HEAP_SMOKE_LOG); \
	$(SMOKE_QEMU_RUN) --log $(HEAP_SMOKE_LOG) --timeout 90 --done 'page_present=1' -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	HEAP_OK=0; \
	grep -q "FASE39_HEAP" $(HEAP_SMOKE_LOG) && grep -q "page_present=1" $(HEAP_SMOKE_LOG) && HEAP_OK=1; \
	$(MAKE) -s build-init-mmap-smoke; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	rm -f $(MMAP_SMOKE_LOG); \
	$(SMOKE_QEMU_RUN) --log $(MMAP_SMOKE_LOG) --timeout 90 \
		--done '[PF] userspace segv pid=' -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	MMAP_OK=0; \
	grep -q "FASE39_MMAP mapped=1" $(MMAP_SMOKE_LOG) && \
	grep -q "FASE39_MMAP.*verify=1" $(MMAP_SMOKE_LOG) && \
	grep -q "\\[PF\\] userspace segv pid=" $(MMAP_SMOKE_LOG) && MMAP_OK=1; \
	$(MAKE) -s build-init-fork-mem-smoke; \
	printf 'PARENT-FILE-OK!' > /tmp/ir0-fase40.dat; \
	python3 scripts/inject_init_minix.py $$DISK /tmp/ir0-fase40.dat etc/f40.dat; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	rm -f $(FORK_MEM_SMOKE_LOG); \
	$(SMOKE_QEMU_RUN) --log $(FORK_MEM_SMOKE_LOG) --timeout 180 \
		--done 'FASE40_SUMMARY A=0 B=0 C=0 D=0' -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK; \
	FORK_OK=0; \
	grep -q "FASE40_SUMMARY A=0 B=0 C=0 D=0" $(FORK_MEM_SMOKE_LOG) && FORK_OK=1; \
	if [ "$$HEAP_OK" != 1 ]; then echo "✗ smoke-mm-cow-lazy FAILED (heap/lazy brk)"; exit 1; fi; \
	if [ "$$MMAP_OK" != 1 ]; then echo "✗ smoke-mm-cow-lazy FAILED (lazy mmap)"; exit 1; fi; \
	if [ "$$FORK_OK" != 1 ]; then \
		echo "✗ smoke-mm-cow-lazy FAILED (fork COW / FASE40)"; \
		grep "FASE40" $(FORK_MEM_SMOKE_LOG) | tail -15; exit 1; \
	fi; \
	echo "✓ smoke-mm-cow-lazy passed (lazy brk + lazy mmap + fork COW FASE40 A–F)"

.PHONY: build/fat16_smoke.img smoke-fat16-mount

build/fat16_smoke.img:
	@chmod +x scripts/create_fat16_smoke_disk.sh
	@./scripts/create_fat16_smoke_disk.sh $(FAT16_SMOKE_IMG)

# D1.18 — read-only FAT16 on secondary ATA disk (hdb), no MINIX root mutation.
smoke-fat16-mount: kernel-x64-userspace.iso build/fat16_smoke.img
	@if [ ! -f disk.img ]; then \
		echo "  DISK    Creating disk.img..."; \
		$(MAKE) -s disk.img; \
	fi
	@echo "  SMOKE   FAT16 mount + read HELLO.TXT on /dev/hdb..."
	@$(MAKE) -s build-init-fat16-smoke
	@DISK=$$(mktemp /tmp/ir0-fat16-smoke.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	python3 scripts/inject_init_minix.py $$DISK $(INIT_SMOKE_BIN) sbin/init; \
	rm -f $(FAT16_SMOKE_LOG); \
	$(SMOKE_QEMU_RUN) --log $(FAT16_SMOKE_LOG) --timeout 90 \
		--done 'FAT16OK' -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-drive file=$(FAT16_SMOKE_IMG),format=raw,if=ide,index=1 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rc=$$?; rm -f $$DISK; \
	if tr -d '\n\r' < $(FAT16_SMOKE_LOG) | grep -q 'FAT16OK'; then \
		echo "✓ smoke-fat16-mount passed (hdb FAT16 mount + HELLO.TXT read)"; \
	elif [ $$rc -ne 0 ]; then \
		echo "✗ smoke-fat16-mount FAILED (QEMU/autokill)"; exit $$rc; \
	else \
		echo "✗ smoke-fat16-mount FAILED (tag missing)"; exit 1; \
	fi

# D1.19 — Linux↔IR0 ABI ground-truth audit (same ELF, strace vs serial)
LINUX_ABI_AUDIT_DIR := build/linux_abi_audit
LINUX_ABI_BRK_PROBE := $(LINUX_ABI_AUDIT_DIR)/brk_probe
LINUX_ABI_WAIT4_PROBE := $(LINUX_ABI_AUDIT_DIR)/wait4_probe
LINUX_ABI_READ_PROBE := $(LINUX_ABI_AUDIT_DIR)/read_probe
LINUX_ABI_MMAP_PROBE := $(LINUX_ABI_AUDIT_DIR)/mmap_probe
LINUX_ABI_MOUNT_PROBE := $(LINUX_ABI_AUDIT_DIR)/mount_probe
LINUX_ABI_OPENAT_PROBE := $(LINUX_ABI_AUDIT_DIR)/openat_probe
LINUX_ABI_STAT_PROBE := $(LINUX_ABI_AUDIT_DIR)/stat_probe
LINUX_ABI_VFS_WRITE_PROBE := $(LINUX_ABI_AUDIT_DIR)/vfs_write_probe
LINUX_ABI_PROCESS_LIFECYCLE_PROBE := $(LINUX_ABI_AUDIT_DIR)/process_lifecycle_probe
LINUX_ABI_KILL_SIGTERM_PROBE := $(LINUX_ABI_AUDIT_DIR)/kill_sigterm_probe
LINUX_ABI_WAIT4_WNOHANG_PROBE := $(LINUX_ABI_AUDIT_DIR)/wait4_wnohang_probe
LINUX_ABI_EXEC_HELPER := $(CURDIR)/$(LINUX_ABI_AUDIT_DIR)/exec_helper

.PHONY: kernel-x64-userspace.iso-fresh

# Force-rebuild userspace ISO so IR0 ABI runners never use a stale kernel.
kernel-x64-userspace.iso-fresh:
	@echo "  ISO     Rebuilding fresh kernel-x64-userspace.iso"
	@rm -f kernel-x64-userspace.bin kernel-x64-userspace.iso \
		kernel/main.o kernel/process/*.o kernel/elf_loader.o \
		kernel/syscalls/process_syscalls.o \
		mm/paging.o arch/common/arch_interface.o kernel/console_backend.o \
		drivers/video/console.o sched/rr_sched.o sched/switch/arch_context_switch.o \
		includes/ir0/signals.o
	@$(MAKE) kernel-x64-userspace.iso


.PHONY: build-linux-abi-brk-probe build-linux-abi-wait4-probe build-linux-abi-wait4-wnohang-probe build-linux-abi-kill-sigterm-probe build-linux-abi-read-probe \
	build-linux-abi-mmap-probe build-linux-abi-mount-probe \
	build-linux-abi-openat-probe build-linux-abi-stat-probe build-linux-abi-vfs-write-probe \
	build-linux-abi-process-lifecycle-probe build-linux-abi-kill-sigterm-probe \
	linux-abi-audit linux-abi-audit-brk linux-abi-audit-wait4 linux-abi-audit-wait4-wnohang linux-abi-audit-kill-sigterm linux-abi-audit-read \
	linux-abi-audit-pipe linux-abi-audit-poll linux-abi-audit-nanosleep linux-abi-audit-getcwd linux-abi-audit-chdir \
	linux-abi-audit-dup linux-abi-audit-mmap linux-abi-audit-mount linux-abi-audit-openat linux-abi-audit-stat \
	linux-abi-audit-execve linux-abi-audit-vfs-write linux-abi-audit-process-lifecycle

build-linux-abi-brk-probe: scripts/linux_abi/workloads/brk_probe.c
	@mkdir -p $(LINUX_ABI_AUDIT_DIR)
	@if command -v musl-gcc >/dev/null 2>&1; then \
		musl-gcc -static -Os -o $(LINUX_ABI_BRK_PROBE) scripts/linux_abi/workloads/brk_probe.c; \
	else \
		gcc -static -Os -o $(LINUX_ABI_BRK_PROBE) scripts/linux_abi/workloads/brk_probe.c; \
	fi
	@echo "✓ $(LINUX_ABI_BRK_PROBE)"

build-linux-abi-wait4-probe: scripts/linux_abi/workloads/wait4_probe.c
	@mkdir -p $(LINUX_ABI_AUDIT_DIR)
	@if command -v musl-gcc >/dev/null 2>&1; then \
		musl-gcc -static -Os -o $(LINUX_ABI_WAIT4_PROBE) scripts/linux_abi/workloads/wait4_probe.c; \
	else \
		gcc -static -Os -o $(LINUX_ABI_WAIT4_PROBE) scripts/linux_abi/workloads/wait4_probe.c; \
	fi
	@echo "✓ $(LINUX_ABI_WAIT4_PROBE)"

build-linux-abi-wait4-wnohang-probe: scripts/linux_abi/workloads/wait4_wnohang_probe.c
	@mkdir -p $(LINUX_ABI_AUDIT_DIR)
	@if command -v musl-gcc >/dev/null 2>&1; then \
		musl-gcc -static -Os -o $(LINUX_ABI_WAIT4_WNOHANG_PROBE) scripts/linux_abi/workloads/wait4_wnohang_probe.c; \
	else \
		gcc -static -Os -o $(LINUX_ABI_WAIT4_WNOHANG_PROBE) scripts/linux_abi/workloads/wait4_wnohang_probe.c; \
	fi
	@echo "✓ $(LINUX_ABI_WAIT4_WNOHANG_PROBE)"

build-linux-abi-kill-sigterm-probe: scripts/linux_abi/workloads/kill_sigterm_probe.c
	@mkdir -p $(LINUX_ABI_AUDIT_DIR)
	@if command -v musl-gcc >/dev/null 2>&1; then \
		musl-gcc -static -Os -o $(LINUX_ABI_KILL_SIGTERM_PROBE) scripts/linux_abi/workloads/kill_sigterm_probe.c; \
	else \
		gcc -static -Os -o $(LINUX_ABI_KILL_SIGTERM_PROBE) scripts/linux_abi/workloads/kill_sigterm_probe.c; \
	fi
	@echo "✓ $(LINUX_ABI_KILL_SIGTERM_PROBE)"

build-linux-abi-read-probe: scripts/linux_abi/workloads/read_probe.c
	@mkdir -p $(LINUX_ABI_AUDIT_DIR)
	@if command -v musl-gcc >/dev/null 2>&1; then \
		musl-gcc -static -Os -o $(LINUX_ABI_READ_PROBE) scripts/linux_abi/workloads/read_probe.c; \
	else \
		gcc -static -Os -o $(LINUX_ABI_READ_PROBE) scripts/linux_abi/workloads/read_probe.c; \
	fi
	@echo "✓ $(LINUX_ABI_READ_PROBE)"

build-linux-abi-mmap-probe: scripts/linux_abi/workloads/mmap_probe.c
	@mkdir -p $(LINUX_ABI_AUDIT_DIR)
	@if command -v musl-gcc >/dev/null 2>&1; then \
		musl-gcc -static -Os -o $(LINUX_ABI_MMAP_PROBE) scripts/linux_abi/workloads/mmap_probe.c; \
	else \
		gcc -static -Os -o $(LINUX_ABI_MMAP_PROBE) scripts/linux_abi/workloads/mmap_probe.c; \
	fi
	@echo "✓ $(LINUX_ABI_MMAP_PROBE)"

build-linux-abi-mount-probe: scripts/linux_abi/workloads/mount_probe.c
	@mkdir -p $(LINUX_ABI_AUDIT_DIR)
	@if command -v musl-gcc >/dev/null 2>&1; then \
		musl-gcc -static -Os -o $(LINUX_ABI_MOUNT_PROBE) scripts/linux_abi/workloads/mount_probe.c; \
	else \
		gcc -static -Os -o $(LINUX_ABI_MOUNT_PROBE) scripts/linux_abi/workloads/mount_probe.c; \
	fi
	@echo "✓ $(LINUX_ABI_MOUNT_PROBE)"

build-linux-abi-openat-probe: scripts/linux_abi/workloads/openat_probe.c
	@mkdir -p $(LINUX_ABI_AUDIT_DIR)
	@if command -v musl-gcc >/dev/null 2>&1; then \
		musl-gcc -static -Os -DOPEN_EXISTING_PATH=\"/proc/uptime\" -o $(LINUX_ABI_OPENAT_PROBE) scripts/linux_abi/workloads/openat_probe.c; \
	else \
		gcc -static -Os -DOPEN_EXISTING_PATH=\"/proc/uptime\" -o $(LINUX_ABI_OPENAT_PROBE) scripts/linux_abi/workloads/openat_probe.c; \
	fi
	@echo "✓ $(LINUX_ABI_OPENAT_PROBE)"

build-linux-abi-stat-probe: scripts/linux_abi/workloads/stat_probe.c
	@mkdir -p $(LINUX_ABI_AUDIT_DIR)
	@if command -v musl-gcc >/dev/null 2>&1; then \
		musl-gcc -static -Os -o $(LINUX_ABI_STAT_PROBE) scripts/linux_abi/workloads/stat_probe.c; \
	else \
		gcc -static -Os -o $(LINUX_ABI_STAT_PROBE) scripts/linux_abi/workloads/stat_probe.c; \
	fi
	@echo "✓ $(LINUX_ABI_STAT_PROBE)"

build-linux-abi-vfs-write-probe: scripts/linux_abi/workloads/vfs_write_probe.c
	@mkdir -p $(LINUX_ABI_AUDIT_DIR)
	@if command -v musl-gcc >/dev/null 2>&1; then \
		musl-gcc -static -Os -o $(LINUX_ABI_VFS_WRITE_PROBE) scripts/linux_abi/workloads/vfs_write_probe.c; \
	else \
		gcc -static -Os -o $(LINUX_ABI_VFS_WRITE_PROBE) scripts/linux_abi/workloads/vfs_write_probe.c; \
	fi
	@echo "✓ $(LINUX_ABI_VFS_WRITE_PROBE)"

build-linux-abi-process-lifecycle-probe: scripts/linux_abi/workloads/process_lifecycle_probe.c scripts/linux_abi/workloads/exec_helper.c
	@mkdir -p $(LINUX_ABI_AUDIT_DIR)
	@if command -v musl-gcc >/dev/null 2>&1; then CC=musl-gcc; else CC=gcc; fi; \
	$$CC -static -Os -o $(LINUX_ABI_AUDIT_DIR)/exec_helper scripts/linux_abi/workloads/exec_helper.c && \
	$$CC -static -Os -DEXEC_HELPER_PATH=\"$(LINUX_ABI_EXEC_HELPER)\" \
		-o $(LINUX_ABI_PROCESS_LIFECYCLE_PROBE) scripts/linux_abi/workloads/process_lifecycle_probe.c
	@echo "✓ $(LINUX_ABI_PROCESS_LIFECYCLE_PROBE)"

verify-minix-rootfs: disk.img
	@python3 scripts/verify_minix_rootfs.py --gate disk.img \
		/sbin /sbin/init /bin/sh /bin/busybox || \
		(echo "✗ verify-minix-rootfs FAILED (rebuild with: make build-runit && setup/runit/install-to-disk.sh disk.img)"; exit 1)
	@echo "✓ verify-minix-rootfs passed"

linux-abi-audit: kernel-x64-userspace.iso build-linux-abi-brk-probe
	@chmod +x scripts/linux_abi/run_linux_brk.sh scripts/linux_abi/run_ir0_brk.sh
	@python3 scripts/linux_abi_audit.py --all
	@grep -q '^## Overall: PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-brk: linux-abi-audit

linux-abi-audit-wait4: kernel-x64-userspace.iso build-linux-abi-wait4-probe
	@chmod +x scripts/linux_abi/run_linux_wait4.sh scripts/linux_abi/run_ir0_wait4.sh
	@python3 scripts/linux_abi_audit.py --contract wait4
	@grep -q '^## wait4 — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-wait4 passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-wait4 FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-wait4-wnohang: kernel-x64-userspace.iso-fresh build-linux-abi-wait4-wnohang-probe kernel-tests
	@chmod +x scripts/linux_abi/run_linux_wait4_wnohang.sh scripts/linux_abi/run_ir0_wait4_wnohang.sh
	@python3 scripts/linux_abi_audit.py --contract wait4_wnohang
	@grep -q '^## wait4_wnohang — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-wait4-wnohang passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-wait4-wnohang FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-kill-sigterm: kernel-x64-userspace.iso-fresh build-linux-abi-kill-sigterm-probe kernel-tests
	@chmod +x scripts/linux_abi/run_linux_kill_sigterm.sh scripts/linux_abi/run_ir0_kill_sigterm.sh
	@python3 scripts/linux_abi_audit.py --contract kill_sigterm
	@grep -q '^## kill_sigterm — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-kill-sigterm passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-kill-sigterm FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-read: kernel-x64-userspace.iso build-linux-abi-read-probe
	@chmod +x scripts/linux_abi/run_linux_read.sh scripts/linux_abi/run_ir0_read.sh
	@python3 scripts/linux_abi_audit.py --contract read
	@grep -q '^## read — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-read passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-read FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-pipe: kernel-x64-userspace.iso build-linux-abi-read-probe
	@chmod +x scripts/linux_abi/run_linux_read.sh scripts/linux_abi/run_ir0_read.sh
	@python3 scripts/linux_abi_audit.py --contract pipe
	@grep -q '^## pipe — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-pipe passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-pipe FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-poll: kernel-x64-userspace.iso
	@chmod +x scripts/linux_abi/run_linux_workload.sh scripts/linux_abi/run_ir0_workload.sh
	@python3 scripts/linux_abi_audit.py --contract poll
	@grep -q '^## poll — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-poll passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-poll FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-nanosleep: kernel-x64-userspace.iso
	@chmod +x scripts/linux_abi/run_linux_workload.sh scripts/linux_abi/run_ir0_workload.sh
	@python3 scripts/linux_abi_audit.py --contract nanosleep
	@grep -q '^## nanosleep — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-nanosleep passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-nanosleep FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-getcwd: kernel-x64-userspace.iso
	@chmod +x scripts/linux_abi/run_linux_workload.sh scripts/linux_abi/run_ir0_workload.sh
	@python3 scripts/linux_abi_audit.py --contract getcwd
	@grep -q '^## getcwd — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-getcwd passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-getcwd FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-chdir: kernel-x64-userspace.iso
	@chmod +x scripts/linux_abi/run_linux_workload.sh scripts/linux_abi/run_ir0_workload.sh
	@python3 scripts/linux_abi_audit.py --contract chdir
	@grep -q '^## chdir — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-chdir passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-chdir FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-dup: kernel-x64-userspace.iso
	@chmod +x scripts/linux_abi/run_linux_workload.sh scripts/linux_abi/run_ir0_workload.sh
	@python3 scripts/linux_abi_audit.py --contract dup
	@grep -q '^## dup — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-dup passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-dup FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-execve: kernel-x64-userspace.iso
	@chmod +x scripts/linux_abi/run_linux_execve.sh scripts/linux_abi/run_ir0_execve.sh 2>/dev/null || true
	@python3 scripts/linux_abi_audit.py --contract execve
	@grep -q '^## execve — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-execve passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-execve FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-mmap: kernel-x64-userspace.iso build-linux-abi-mmap-probe
	@chmod +x scripts/linux_abi/run_linux_mmap.sh scripts/linux_abi/run_ir0_mmap.sh
	@python3 scripts/linux_abi_audit.py --contract mmap
	@grep -q '^## mmap — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-mmap passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-mmap FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-mount: kernel-x64-userspace.iso build-linux-abi-mount-probe
	@chmod +x scripts/linux_abi/run_linux_mount.sh scripts/linux_abi/run_ir0_mount.sh
	@python3 scripts/linux_abi_audit.py --contract mount
	@grep -q '^## mount — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-mount passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-mount FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-openat: kernel-x64-userspace.iso build-linux-abi-openat-probe
	@chmod +x scripts/linux_abi/run_linux_openat.sh scripts/linux_abi/run_ir0_openat.sh
	@python3 scripts/linux_abi_audit.py --contract openat
	@grep -q '^## openat — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-openat passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-openat FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-stat: kernel-x64-userspace.iso build-linux-abi-stat-probe
	@chmod +x scripts/linux_abi/run_linux_stat.sh scripts/linux_abi/run_ir0_stat.sh
	@python3 scripts/linux_abi_audit.py --contract stat
	@grep -q '^## stat — PASS' $(LINUX_ABI_AUDIT_DIR)/report.md && \
		echo "✓ linux-abi-audit-stat passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-stat FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

linux-abi-audit-vfs-write: kernel-x64-userspace.iso build-linux-abi-vfs-write-probe
	@chmod +x scripts/linux_abi/run_linux_vfs_write.sh scripts/linux_abi/run_ir0_vfs_write.sh
	@python3 scripts/linux_abi_audit.py --contract vfs_write || true
	@if grep -q 'bundle_status: VERIFIED' $(LINUX_ABI_AUDIT_DIR)/report.md; then \
		echo "✓ linux-abi-audit-vfs-write VERIFIED (see $(LINUX_ABI_AUDIT_DIR)/report.md)"; \
	elif grep -q 'bundle_status: PARTIAL' $(LINUX_ABI_AUDIT_DIR)/report.md; then \
		echo "△ linux-abi-audit-vfs-write PARTIAL (see $(LINUX_ABI_AUDIT_DIR)/report.md)"; \
	else \
		(echo "✗ linux-abi-audit-vfs-write BLOCKED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1; \
	fi

linux-abi-audit-process-lifecycle: kernel-x64-userspace.iso build-linux-abi-process-lifecycle-probe
	@chmod +x scripts/linux_abi/run_linux_process_lifecycle.sh scripts/linux_abi/run_ir0_process_lifecycle.sh
	@python3 scripts/linux_abi_audit.py --contract process_lifecycle --report-dir $(LINUX_ABI_AUDIT_DIR) && \
		echo "✓ linux-abi-audit-process-lifecycle passed (see $(LINUX_ABI_AUDIT_DIR)/report.md)" || \
		(echo "✗ linux-abi-audit-process-lifecycle FAILED — see $(LINUX_ABI_AUDIT_DIR)/report.md"; exit 1)

smoke-runit-ash-interactive: kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then \
		echo "  DISK    disk.img missing — running load-userspace-runit"; \
		$(MAKE) -s load-userspace-runit; \
	fi
	@echo "  SMOKE   runit PID1 + ash interactive (headless + monitor sendkey)..."
	@chmod +x scripts/smoke_runit_ash_interactive.py
	@python3 scripts/smoke_runit_ash_interactive.py --log $(RUNIT_ASH_SMOKE_LOG) --timeout 120 --iso kernel-x64-userspace.iso --disk disk.img
	@echo "  LOG     $(RUNIT_ASH_SMOKE_LOG)"

# T1 GUI — runit → BusyBox ash on /dev/console (tier1 stable; not legacy-only).
.PHONY: run-fase58e-ash-gui check-fase58e-logs

run-fase58e-ash-gui: load-userspace-runit kernel-x64-userspace.iso
	@case "$(FASE58E_DISPLAY)" in none|headless) \
		echo "✗ FASE58E ash GUI blocked: FASE58E_DISPLAY=$(FASE58E_DISPLAY)"; exit 1;; esac
	@echo "  FASE58E   runit → ash on /dev/console"
	@echo "  QEMU     display=$(FASE58E_DISPLAY)"
	@echo "  LOG      serial -> $(FASE58E_ASH_LOG)"
	@echo "  HINT     click QEMU window; try: ls / pwd / echo hi"
	@echo "  HINT     Doom manual: doomgeneric /usr/share/doom/doom1.wad"
	@rm -f $(FASE58E_ASH_LOG); \
	DISK=$$(mktemp /tmp/ir0-fase58e-ash.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	if [ -n "$(REAL_WAD_PATH)" ] && [ -f "$(REAL_WAD_PATH)" ]; then \
		$(MAKE) -s build-fase55e-doom-interactive; \
		CFG=$$(mktemp /tmp/doom-frames-cfg.XXXXXX); \
		printf '0\n0\n' > $$CFG; \
		python3 scripts/inject_init_minix.py $$DISK $(FASE55E_DOOM_BIN) bin/doomgeneric && \
		python3 scripts/inject_init_minix.py $$DISK "$(REAL_WAD_PATH)" usr/share/doom/doom1.wad && \
		python3 scripts/inject_init_minix.py $$DISK $$CFG etc/doom-frames && \
		rm -f $$CFG; \
	fi; \
	python3 scripts/verify_minix_rootfs.py $$DISK /sbin/init /bin/sh /bin/busybox; \
	if [ "$(FASE58E_DISPLAY)" = "sdl" ]; then \
		DISP="-display sdl2"; \
	else \
		DISP="-display gtk"; \
	fi; \
	$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial file:$(FASE58E_ASH_LOG) \
		$$DISP -m 256M -no-reboot -net none; \
	rm -f $$DISK

check-fase58e-logs:
	@echo "=== FASE58E/K (runit ash GUI + compact smoke tags) ==="
	@if [ -f "$(FASE58E_ASH_LOG)" ]; then \
		grep -E 'RUNIT_STAGE1_OK|RUNIT_STAGE2_OK|RUNSV_CONSOLE_START|ASH_INTERACTIVE_READY|KBD_USER_POLL_OK|TTY_CANON_LINE_READY|SYS_READ_RETURN_OK|ASH_COMMAND_ECHO_OK|ASH_COMMAND_EXEC_OK' "$(FASE58E_ASH_LOG)" || echo "(no FASE58E/K tags)"; \
	else echo "missing $(FASE58E_ASH_LOG)"; fi
	@if [ -f "$(FASE58E_ASH_SMOKE_LOG)" ]; then \
		echo "=== FASE58E ash smoke ($(FASE58E_ASH_SMOKE_LOG)) ==="; \
		grep -E 'RUNIT_STAGE1_OK|RUNIT_STAGE2_OK|RUNSV_CONSOLE_START|ASH_INTERACTIVE_READY|KBD_USER_POLL_OK|TTY_CANON_LINE_READY|SYS_READ_RETURN_OK|ASH_COMMAND_ECHO_OK|ASH_COMMAND_EXEC_OK' "$(FASE58E_ASH_SMOKE_LOG)" || echo "(no smoke tags)"; \
	fi

build-tcc-fase52:
	@./setup/tcc/build-fase52.sh

build-init-fase52-tcc:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  HARNESS Building FASE52 TCC smoke ($(FASE52_HARNESS_BIN))"
	@$(MUSL_CC) -static -Os -o $(FASE52_HARNESS_BIN) $(INIT_FASE52_TCC_SRC)
	@file $(FASE52_HARNESS_BIN) | grep -q ELF
	@echo "✓ build-init-fase52-tcc OK"

build-init-fase53a-fs-dev:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE53A fs/dev smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE53A_FS_DEV_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase53a-fs-dev OK"

build-init-fase53b-posix-pseudofs:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE53B posix/pseudo-fs smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE53B_POSIX_PSEUDOFS_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase53b-posix-pseudofs OK"

build-init-heart-smoke:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building /heart smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_HEART_SMOKE_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-heart-smoke OK"

build-init-fase54a-fbdev:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE54A fbdev smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE54A_FBDEV_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase54a-fbdev OK"

build-fase58c-boot-halt:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    FASE58C boot-halt probe ($(FASE58C_BOOT_BIN))"
	@$(MUSL_CC) -static -Os -o $(FASE58C_BOOT_BIN) $(INIT_FASE58C_BOOT_HALT_SRC)
	@file $(FASE58C_BOOT_BIN) | grep -q ELF
	@echo "✓ build-fase58c-boot-halt OK"

build-fase58c-fbdev:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    FASE58C fbdev probe ($(FASE58C_FBDEV_BIN))"
	@$(MUSL_CC) -static -Os -o $(FASE58C_FBDEV_BIN) $(INIT_FASE58C_FBDEV_SRC)
	@file $(FASE58C_FBDEV_BIN) | grep -q ELF
	@echo "✓ build-fase58c-fbdev OK"

build-init-fase54b-input:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE54B input smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE54B_INPUT_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase54b-input OK"

build-init-fase54c-input-deterministic:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE54C deterministic input smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE54C_INPUT_DET_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase54c-input-deterministic OK"

build-init-fase55a-doom-prereq:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE55A doom prereq smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE55A_DOOM_PREREQ_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase55a-doom-prereq OK"

build-init-fase55b-doom-stub:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE55B doom stub ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE55B_DOOM_STUB_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase55b-doom-stub OK"

build-init-fase55c-timing-input:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE55C timing+input smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE55C_TIMING_INPUT_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase55c-timing-input OK"

build-init-fase55d-doomgeneric:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  HARNESS Building FASE55D real doomgeneric ($(FASE55D_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -s -ffunction-sections -fdata-sections \
		-Wl,--gc-sections -Wl,--strip-all -std=gnu99 \
		-DIR0_DOOM_PORT \
		-Isetup/doom/upstream/doomgeneric \
		$(INIT_FASE55D_DOOMGENERIC_SRC) \
		setup/doom/upstream/doomgeneric/*.c \
		-o $(FASE55D_SMOKE_BIN) -lm
	@file $(FASE55D_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase55d-doomgeneric OK"

build-fase55e-doom-interactive:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  DOOM    Building FASE55E interactive doomgeneric ($(FASE55E_DOOM_BIN))"
	@$(MUSL_CC) -static -Os -s -ffunction-sections -fdata-sections \
		-Wl,--gc-sections -Wl,--strip-all -std=gnu99 \
		-DFASE55E_INTERACTIVE=1 -DIR0_DOOM_PORT \
		-Isetup/doom/upstream/doomgeneric \
		$(INIT_FASE55D_DOOMGENERIC_SRC) \
		setup/doom/upstream/doomgeneric/*.c \
		-o $(FASE55E_DOOM_BIN) -lm
	@file $(FASE55E_DOOM_BIN) | grep -q ELF
	@echo "✓ build-fase55e-doom-interactive OK"

build-init-fase50-programs:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  INIT    Building FASE50 programs smoke ($(INIT_SMOKE_BIN))"
	@$(MUSL_CC) -static -Os -o $(INIT_SMOKE_BIN) $(INIT_FASE50_PROGRAMS_SRC)
	@file $(INIT_SMOKE_BIN) | grep -q ELF
	@echo "✓ build-init-fase50-programs OK"

# Kernel ISO booting /sbin/init (CONFIG_KERNEL_DEBUG_SHELL=n)
# Default: lazy anon mmap + brk (defconfig LAZY_*=y). Bisect: make kernel-x64-userspace-eager.bin
kernel-x64-userspace.bin:
	@rm -f kernel/main.o kernel/process/*.o kernel/elf_loader.o \
		mm/paging.o arch/common/arch_interface.o kernel/console_backend.o \
		drivers/video/console.o sched/rr_sched.o
	@$(MAKE) kernel-x64.bin USERSPACE_INIT_BUILD=1
	@cp kernel-x64.bin $@
	@rm -f kernel/main.o kernel/process/*.o
	@$(MAKE) kernel-x64.bin
	@echo "✓ Kernel (userspace init, lazy MM) copied: $@"

kernel-x64-userspace-eager.bin:
	@rm -f kernel/main.o kernel/process/*.o kernel/elf_loader.o \
		mm/paging.o arch/common/arch_interface.o kernel/console_backend.o \
		drivers/video/console.o sched/rr_sched.o
	@$(MAKE) kernel-x64.bin USERSPACE_INIT_BUILD=1 USERSPACE_EAGER_MM=1
	@cp kernel-x64.bin $@
	@rm -f kernel/main.o kernel/process/*.o
	@$(MAKE) kernel-x64.bin
	@echo "✓ Kernel (userspace init, eager MM bisect) copied: $@"

# Back-compat alias (same kernel as kernel-x64-userspace.bin).
kernel-x64-userspace-lazy.bin: kernel-x64-userspace.bin
	@cp kernel-x64-userspace.bin $@
	@echo "✓ Kernel (lazy MM alias) copied: $@"

kernel-x64-userspace.iso: kernel-x64-userspace.bin arch/x86-64/grub.cfg
	@echo "  ISO     $@ (userspace init boot, lazy MM)"
	@rm -rf iso_userspace
	@mkdir -p iso_userspace/boot/grub
	@cp arch/x86-64/grub.cfg iso_userspace/boot/grub/
	@cp kernel-x64-userspace.bin iso_userspace/boot/kernel-x64.bin
	@grub-mkrescue -o $@ iso_userspace
	@rm -rf iso_userspace
	@echo "✓ ISO (userspace init, lazy MM) created: $@"

kernel-x64-userspace-lazy.iso: kernel-x64-userspace.iso
	@cp kernel-x64-userspace.iso $@
	@echo "✓ ISO (lazy MM alias) created: $@"

kernel-x64-userspace-eager.iso: kernel-x64-userspace-eager.bin arch/x86-64/grub.cfg
	@echo "  ISO     $@ (userspace init boot, eager MM bisect)"
	@rm -rf iso_userspace_eager
	@mkdir -p iso_userspace_eager/boot/grub
	@cp arch/x86-64/grub.cfg iso_userspace_eager/boot/grub/
	@cp kernel-x64-userspace-eager.bin iso_userspace_eager/boot/kernel-x64.bin
	@grub-mkrescue -o $@ iso_userspace_eager
	@rm -rf iso_userspace_eager
	@echo "✓ ISO (userspace init, eager MM bisect) created: $@"

# --- Validation (tooling entry points; historical smokes: IR0_LEGACY_SMOKE=1) ---
.PHONY: ctr smoke-tier1 test-fast ktm ktm-check ktm-manifest ktm-classify ktm-classify-selftest ktm-report

ctr:
	@chmod +x scripts/ctr.sh
	@./scripts/ctr.sh

test-fast: kernel-x64.bin arch-guard
	@$(MAKE) -s -C tests/host run

smoke-tier1: kernel-x64.bin arch-guard
	@$(MAKE) -s smoke-runit-boot
	@$(MAKE) -s smoke-runit-ash-interactive

# Release 0.0.1 gate — deterministic regression bundle (D1.20).
.PHONY: smoke-release-0.0.1 release-0.0.1

smoke-release-0.0.1:
	@echo "  RELEASE 0.0.1 gate (D1.20 deterministic bundle)"
	@$(MAKE) -s roadmap-phase1-stability
	@$(MAKE) -s linux-abi-audit
	@$(MAKE) -s smoke-runit-ash-interactive
	@$(MAKE) -s smoke-fat16-mount
	@echo "✓ smoke-release-0.0.1 passed"

release-0.0.1: kernel-text-budget smoke-release-0.0.1
	@echo "✓ release-0.0.1 gate passed (kernel-text-budget + smoke-release-0.0.1)"

ktm: ktm-check

ktm-check: kernel-x64.bin arch-guard
	@$(MAKE) -s -C tests/host run
	@python3 scripts/ktm_classify_selftest.py
	@python3 scripts/ktm_panic_inventory.py --check
	@python3 scripts/ktm_syscall_manifest.py --tier1 || true
	@echo "✓ KTM check complete (tier-1 manifest gaps are informational)"

ktm-classify-selftest:
	@python3 scripts/ktm_classify_selftest.py

ktm-manifest:
	@python3 scripts/ktm_syscall_manifest.py --tier1

ktm-classify:
	@python3 scripts/ktm_log_classify.py $(or $(LOG),/tmp/runit-ash-smoke.log)

ktm-report:
	@python3 scripts/ktm_report.py $(or $(LOG),/tmp/runit-ash-smoke.log)

ifdef IR0_LEGACY_SMOKE
include $(KERNEL_ROOT)/setup/make/legacy-smokes.mk
endif

clean:
	@echo "Cleaning build artifacts..."
	@find . -name "*.o" -type f -delete
	@find . -name "*.d" -type f -delete
	@find . -name "*.bin" -type f -delete
	@find . -name "*.su" -type f -delete
	@rm -f kernel-x64.iso kernel-x64-test.iso
	@rm -f kernel-x64.map kernel-arm64.map kernel-x64.disasm compile_commands.json
	@rm -rf iso iso_test
	@$(MAKE) -C tests/kernel_memsafe clean 2>/dev/null || true
	@echo "Clean done."

# TEST SUITE — Compila todos los artefactos de test (estilo kernels de producción).
# - Código kernel en host para Valgrind (make kernel-memsafe)
# - Kernel con tests in-kernel (ktest) para make kernel-tests
# - Análisis del binario de arranque (make kernel-analyze → kernel-x64.bin)
tests: kernel-x64-test.bin
	@echo "  TEST    Building kernel-memsafe (kernel code under Valgrind)..."
	@$(MAKE) -C tests/kernel_memsafe KERNEL_ROOT=$(KERNEL_ROOT) all
	@echo "  TEST    Running host test suite..."
	@$(MAKE) -C tests/host run
	@echo "✓ Tests built: tests/kernel_memsafe/ir0_kernel_memsafe, kernel-x64-test.bin"

# Valgrind sobre código del kernel compilado para host (resource_registry, etc.)
kernel-memsafe:
	@echo "  KERNEL-MEMSAFE  Building kernel code for host and running Valgrind..."
	@$(MAKE) -C tests/kernel_memsafe KERNEL_ROOT=$(KERNEL_ROOT) memsafe
	@echo "✓ kernel-memsafe passed"

# Batería in-kernel al estilo KUnit: tests se ejecutan al arranque (no dependen de la shell).
# QEMU headless, sin red. El kernel (kernel-x64-test.bin) llama kernel_test_run_all() en boot.
kernel-tests: kernel-x64-test.iso disk.img
	@echo "  KTEST   Running in-kernel test suite at boot (KUnit-style)..."
	@$(SMOKE_QEMU_RUN) --log /tmp/ktest.log --timeout 60 --done "test(s) passed" \
		--fail-regex 'Some tests FAILED|not ok ' -- \
		$(QEMU) -cdrom kernel-x64-test.iso -drive file=disk.img,format=raw,if=ide,index=0 -serial stdio -display none -m 128M -no-reboot -net none; \
	grep -q "All .* test(s) passed" /tmp/ktest.log && ! grep -q "Some tests FAILED" /tmp/ktest.log && ! grep -q "not ok " /tmp/ktest.log && ! grep -q "# SKIP need process" /tmp/ktest.log; \
	if [ $$? -eq 0 ]; then echo "✓ kernel-tests passed"; exit 0; else echo "✗ kernel-tests FAILED"; exit 1; fi

smoke-multiuser-perms: kernel-tests
	@grep -q "MULTIUSER_PERMS_OK" /tmp/ktest.log || \
		(echo "✗ smoke-multiuser-perms FAILED (tag missing)"; exit 1)
	@echo "✓ smoke-multiuser-perms passed"

.PHONY: smoke-multiuser-perms smoke-musl-pthread smoke-setuid-exec build-musl-pthread-smoke build-su-setuid-smoke

smoke-setuid-exec: build-su-setuid-smoke kernel-x64-userspace.iso
	@echo "  SMOKE   setuid-root exec (S_ISUID + setresuid drop)..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=64 status=none && \
	python3 scripts/inject_init_minix.py --format $$DISK && \
	python3 scripts/inject_init_minix.py --setuid $$DISK $(SU_SETUID_SMOKE_BIN) sbin/init && \
	python3 scripts/verify_minix_rootfs.py $$DISK /sbin/init && \
	$(SMOKE_QEMU_RUN) --log $(SU_SETUID_SMOKE_LOG) --profile musl-arch-prctl \
		--done SU_SETUID_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 128M -no-reboot -net none; \
	rm -f $$DISK;
	@grep -q "SU_SETUID_OK" $(SU_SETUID_SMOKE_LOG) && \
		echo "✓ smoke-setuid-exec passed" || \
		(echo "✗ smoke-setuid-exec FAILED"; exit 1)

smoke-musl-pthread: build-musl-pthread-smoke kernel-x64-userspace.iso
	@echo "  SMOKE   musl pthread_create + join..."
	@DISK=$$(mktemp /tmp/ir0-userspace-disk.XXXXXX.img); \
	dd if=/dev/zero of=$$DISK bs=1M count=64 status=none && \
	python3 scripts/inject_init_minix.py --format $$DISK && \
	python3 scripts/inject_init_minix.py $$DISK $(MUSL_PTHREAD_SMOKE_BIN) sbin/init && \
	python3 scripts/verify_minix_rootfs.py $$DISK /sbin/init && \
	$(SMOKE_QEMU_RUN) --log $(MUSL_PTHREAD_SMOKE_LOG) --profile musl-pthread \
		--done MUSL_PTHREAD_OK -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 128M -no-reboot -net none; \
	rm -f $$DISK;
	@grep -q "MUSL_PTHREAD_OK" $(MUSL_PTHREAD_SMOKE_LOG) && \
		echo "✓ smoke-musl-pthread passed" || \
		(echo "✗ smoke-musl-pthread FAILED"; exit 1)

# Análisis del binario del kernel: secciones (size -A), símbolos no definidos, kmain.
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
KERNEL_TEXT_BUDGET ?= 850000

kernel-text-budget: kernel-x64.bin
	@TEXT=$$(size -A kernel-x64.bin | awk '/^\.text/{print $$2}'); \
	echo "kernel .text = $$TEXT bytes (budget $(KERNEL_TEXT_BUDGET))"; \
	if [ -z "$$TEXT" ] || [ "$$TEXT" -gt "$(KERNEL_TEXT_BUDGET)" ]; then \
		echo "✗ kernel-text-budget FAILED"; exit 1; \
	fi; \
	echo "✓ kernel-text-budget passed"

# Salud del sistema: ejecuta toda la batería (kernel-analyze, kernel-memsafe, kernel-tests).
# Útil para CI o comprobar que el árbol está sano antes de un commit.
health: kernel-analyze kernel-text-budget
	@echo ""
	@echo "  HEALTH  Running full test suite..."
	@$(MAKE) kernel-memsafe && $(MAKE) kernel-tests
	@echo ""
	@echo "✓ health passed (kernel-analyze, kernel-text-budget, kernel-memsafe, kernel-tests)"

# Minimal permanent build matrix for modular configs.
build-matrix-min:
	@$(MAKE) -s config-wiring-check
	@$(MAKE) -s arch-config-check
	@echo "  MATRIX  defconfig"
	@$(MAKE) defconfig >/dev/null
	@$(MAKE) -s kernel-x64.bin >/dev/null
	@echo "  MATRIX  tiny preset"
	@python3 $(KERNEL_ROOT)/scripts/kconfig/menuconfig.py --preset tiny >/dev/null
	@$(MAKE) -s kernel-x64.bin >/dev/null
	@echo "  MATRIX  networking disabled"
	@$(MAKE) defconfig >/dev/null
	@python3 $(KERNEL_ROOT)/scripts/kconfig/menuconfig.py --set ENABLE_NETWORKING=n INIT_NETWORK_STACK=n >/dev/null
	@$(MAKE) -s kernel-x64.bin >/dev/null
	@echo "  MATRIX  storage fully disabled"
	@$(MAKE) defconfig >/dev/null
	@python3 $(KERNEL_ROOT)/scripts/kconfig/menuconfig.py --set ENABLE_STORAGE_ATA=n ENABLE_STORAGE_ATA_BLOCK=n INIT_STORAGE_ATA=n INIT_STORAGE_ATA_BLOCK=n >/dev/null
	@$(MAKE) -s kernel-x64.bin >/dev/null
	@echo "  MATRIX  storage core without block layer"
	@$(MAKE) defconfig >/dev/null
	@python3 $(KERNEL_ROOT)/scripts/kconfig/menuconfig.py --set ENABLE_STORAGE_ATA=y ENABLE_STORAGE_ATA_BLOCK=n INIT_STORAGE_ATA=y INIT_STORAGE_ATA_BLOCK=n >/dev/null
	@$(MAKE) -s kernel-x64.bin >/dev/null
	@echo "  MATRIX  tmpfs-only root"
	@$(MAKE) defconfig >/dev/null
	@python3 $(KERNEL_ROOT)/scripts/kconfig/menuconfig.py --set ENABLE_FS_MINIX=n ENABLE_FS_TMPFS=y ROOT_FILESYSTEM=tmpfs >/dev/null
	@$(MAKE) -s kernel-x64.bin >/dev/null
	@echo "  MATRIX  scheduler policy 1"
	@$(MAKE) defconfig >/dev/null
	@python3 $(KERNEL_ROOT)/scripts/kconfig/menuconfig.py --set SCHEDULER_POLICY=1 >/dev/null
	@$(MAKE) -s kernel-x64.bin >/dev/null
	@echo "  MATRIX  scheduler policy 2"
	@$(MAKE) defconfig >/dev/null
	@python3 $(KERNEL_ROOT)/scripts/kconfig/menuconfig.py --set SCHEDULER_POLICY=2 >/dev/null
	@$(MAKE) -s kernel-x64.bin >/dev/null
	@echo "  MATRIX  arm64 config scaffold"
	@$(MAKE) defconfig >/dev/null
	@python3 $(KERNEL_ROOT)/scripts/kconfig/menuconfig.py --set ARCH_X86_64=n ARCH_ARM64=y DRV_NIC_RTL8139=n DRV_NIC_E1000=n >/dev/null
	@$(MAKE) -s arch-config-check >/dev/null
	@$(MAKE) defconfig >/dev/null
	@echo "  MATRIX  USB host enabled"
	@$(MAKE) defconfig >/dev/null
	@python3 $(KERNEL_ROOT)/scripts/kconfig/menuconfig.py --set ENABLE_USB_HOST=y INIT_USB_HOST=y >/dev/null
	@$(MAKE) -s kernel-x64.bin >/dev/null
	@echo "  MATRIX  Bluetooth disabled"
	@$(MAKE) defconfig >/dev/null
	@python3 $(KERNEL_ROOT)/scripts/kconfig/menuconfig.py --set ENABLE_BLUETOOTH=n INIT_BLUETOOTH_DRIVER=n DEBUG_BINS_GROUP_BT=n >/dev/null
	@$(MAKE) -s kernel-x64.bin >/dev/null
	@echo "  MATRIX  lazy MM disabled (eager mmap/brk bisect)"
	@$(MAKE) defconfig >/dev/null
	@python3 $(KERNEL_ROOT)/scripts/kconfig/menuconfig.py --set LAZY_ANON_MMAP=n LAZY_BRK_HEAP=n >/dev/null
	@$(MAKE) -s kernel-x64.bin >/dev/null
	@$(MAKE) defconfig >/dev/null
	@$(MAKE) -s arch-guard
	@echo "✓ build-matrix-min passed"

config-sim:
	@python3 $(KERNEL_ROOT)/scripts/kconfig/config_sim.py

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

runtime-net-check:
	@echo "  RUNTIME  network smoke (QEMU user-net)"
	@$(MAKE) -s defconfig >/dev/null
	@python3 $(KERNEL_ROOT)/scripts/kconfig/config_sim.py \
		--symbols ENABLE_NETWORKING,INIT_NETWORK_STACK \
		--max-cases 2 \
		--build-cmd "make -s kernel-x64.iso disk.img" \
		--runtime-cmd "python3 $(KERNEL_ROOT)/scripts/net_runtime_smoke.py --timeout-sec 25"
	@echo "✓ runtime-net-check passed"

runtime-mount-check: kernel-tests
	@echo "  RUNTIME  mount contract smoke (QEMU)"
	@grep -q "ok .* - mount_proc_contract" /tmp/ktest.log && \
	 grep -q "ok .* - mount_tmpfs_contract" /tmp/ktest.log && \
	 grep -q "ok .* - mount_multi_fs_contract" /tmp/ktest.log && \
	 grep -q "ok .* - mount_longest_prefix_contract" /tmp/ktest.log && \
	 grep -q "ok .* - block_hda_read_contract" /tmp/ktest.log
	@echo "✓ runtime-mount-check passed"

arch-guard:
	@python3 $(KERNEL_ROOT)/scripts/architecture_guard.py

repo-hygiene-guard:
	@python3 $(KERNEL_ROOT)/scripts/repo_hygiene_guard.py

build-matrix-full:
	@$(MAKE) -s build-matrix-min
	@echo "  MATRIX  boolean config simulation (24 cases)"
	@python3 $(KERNEL_ROOT)/scripts/kconfig/config_sim.py --max-cases 24 --build-cmd "make -s kernel-x64.bin"
	@echo "  MATRIX  runtime network smoke checks"
	@$(MAKE) -s runtime-net-check
	@echo "  MATRIX  runtime mount smoke checks"
	@$(MAKE) -s runtime-mount-check
	@echo "  MATRIX  architecture guardrails"
	@$(MAKE) -s arch-guard
	@echo "  MATRIX  repository hygiene guardrails"
	@$(MAKE) -s repo-hygiene-guard
	@echo "✓ build-matrix-full passed"

smoke-qemu:
	@echo "  SMOKE   qemu baseline"
	@$(MAKE) -s kernel-tests
	@$(MAKE) -s runtime-net-check
	@$(MAKE) -s runtime-mount-check
	@echo "✓ smoke-qemu passed"

smoke-real-hw:
	@echo "  SMOKE   real hardware checklist"
	@bash $(KERNEL_ROOT)/tests/smoke/run_real_hw_smoke.sh
	@echo "✓ smoke-real-hw checklist generated"

smoke-all:
	@$(MAKE) -s smoke-qemu
	@$(MAKE) -s smoke-real-hw
	@echo "✓ smoke-all completed"

roadmap-phase1-stability:
	@echo "  ROADMAP phase1 stability gates"
	@$(MAKE) -s kernel-x64.bin
	@$(MAKE) -s tests
	@$(MAKE) -s kernel-tests
	@$(MAKE) -s kernel-memsafe
	@$(MAKE) -s kernel-analyze
	@$(MAKE) -s build-matrix-min
	@$(MAKE) -s arch-guard
	@$(MAKE) -s smoke-mm-cow-lazy
	@echo "✓ roadmap phase1 ready"

roadmap-phase2-driver-expansion: roadmap-phase1-stability
	@echo "  ROADMAP phase2 driver expansion gate"
	@$(MAKE) -s runtime-net-check
	@$(MAKE) -s runtime-mount-check
	@echo "✓ roadmap phase2 ready (modern net/storage driver work can proceed)"

roadmap-phase3-core-features: roadmap-phase2-driver-expansion
	@echo "  ROADMAP phase3 core feature gate"
	@echo "✓ roadmap phase3 ready (network/process semantic expansion can proceed)"

scale-readiness-gate:
	@$(MAKE) -s build-matrix-full
	@echo "✓ scale-readiness-gate passed"
