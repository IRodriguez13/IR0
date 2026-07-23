# SPDX-License-Identifier: GPL-3.0-only
# Class B (KERNEL_CS + userspace RIP) KTM inject + repair gates.
# Requires: MUSL_CC, KTM_USERDEV_*, SMOKE_QEMU_RUN / ktm_userdev_runner.py,
#           build-init-hostshare-exec, kernel-x64-userspace.iso

ifndef KTM_USERDEV_DIR
KTM_USERDEV_DIR = tests/ktm/userdev
endif
ifndef KTM_USERDEV_LIB_SRC
KTM_USERDEV_LIB_SRC = tests/ktm/lib/libktm_user.c
endif
ifndef KTM_USERDEV_MUSL_FLAGS
KTM_USERDEV_MUSL_FLAGS = -static -Os -Itests/ktm/lib -idirafter includes
endif

KTM_FAULT_CLASS_B_SRC ?= $(KTM_USERDEV_DIR)/ktm_fault_class_b_case.c $(KTM_USERDEV_LIB_SRC)
KTM_FAULT_CLASS_B_BIN ?= $(KTM_USERDEV_DIR)/ktm_fault_class_b_case

.PHONY: build-ktm-fault-class-b-case ktm-userdev-fault-class-b-run \
	smoke-class-b-mitigated smoke-class-b-repro

build-ktm-fault-class-b-case:
	@if [ -z "$(MUSL_CC)" ]; then \
		echo "✗ musl cross compiler not found (install musl-tools or set MUSL_CC=...)"; \
		exit 1; \
	fi
	@echo "  KTM     Building fault_class_b pilot ($(KTM_FAULT_CLASS_B_BIN))"
	@$(MUSL_CC) $(KTM_USERDEV_MUSL_FLAGS) \
		-o $(KTM_FAULT_CLASS_B_BIN) $(KTM_FAULT_CLASS_B_SRC)
	@file $(KTM_FAULT_CLASS_B_BIN) | grep -q ELF
	@echo "✓ build-ktm-fault-class-b-case OK"

# Class B inject with product repair (default IR0_CLASS_B_REPAIR=1).
ktm-userdev-fault-class-b-run: build-ktm-fault-class-b-case build-init-hostshare-exec kernel-x64-userspace.iso
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_FAULT_CLASS_B_BIN) \
		--log /tmp/ktm-userdev-fault-class-b.log --timeout 90 \
		--done KTM_CLASS_B_OK \
		--require CLASS_B_FAULT_INJECT \
		--require KERNEL_CS_USER_RIP_REPAIR \
		--require KTM_CLASS_B_MITIGATED_OK \
		--require KTM_CLASS_B_OK \
		--require KTM_USERDEV_OK
	@echo "✓ ktm-userdev-fault-class-b-run (Class B inject + REPAIR)"

smoke-class-b-mitigated: ktm-userdev-fault-class-b-run

# Deterministic Class B repro: rebuild switch path with REPAIR off, expect BAD_RIP.
smoke-class-b-repro: build-ktm-fault-class-b-case build-init-hostshare-exec
	@echo "  CLASS_B Rebuild kernel with IR0_CLASS_B_REPAIR=0..."
	@rm -f sched/switch/arch_context_switch.o
	@$(MAKE) -s kernel-x64-userspace.iso CFLAGS_TARGET="$(CFLAGS_TARGET) -DIR0_CLASS_B_REPAIR=0"
	@if [ ! -f disk.img ]; then $(MAKE) -s disk.img; fi
	@python3 scripts/ktm_userdev_runner.py \
		--init $(KTM_FAULT_CLASS_B_BIN) \
		--log /tmp/ktm-userdev-fault-class-b-repro.log --timeout 90 \
		--done KERNEL_RET_BAD_RIP \
		--require CLASS_B_FAULT_INJECT \
		--require KERNEL_RET_BAD_RIP
	@echo "✓ smoke-class-b-repro (inject + REPAIR=0 → KERNEL_RET_BAD_RIP)"
	@echo "  CLASS_B Restoring product kernel (IR0_CLASS_B_REPAIR=1)..."
	@rm -f sched/switch/arch_context_switch.o
	@$(MAKE) -s kernel-x64-userspace.iso
	@echo "✓ product ISO restored"
