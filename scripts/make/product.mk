# SPDX-License-Identifier: GPL-3.0-only
#
# IR0 product integration (machines, kmang, first-boot, QEMU product run).
# Included from scripts/make/isd.mk after bridge variables are defined.

ifndef _IR0_PRODUCT_MK
_IR0_PRODUCT_MK := 1

.PHONY: bootstrap-userspace first-boot machine-create machine-reset machine-info \
	kernel-manager-install kernel-manager-list \
	kmang kmang-cli kmang-test tui-k \
	usmang usmang-test usmang-verify tui-u \
	machine-update-kernel machine-update-userspace \
	ensure-machine-desktop-sync machine-migrate-home image-vmware poweron run-isd \
	smoke-runit-boot-isd smoke-init-boot-isd smoke-ext2-root-boot init-cap init-smoke \
	init-smoke-matrix root-fs-smoke-matrix

INIT_BOOT_PY = python3 scripts/init_boot_capture.py

# Deprecated alias → new bootstrap
bootstrap-userspace:
	@echo "note: bootstrap-userspace is deprecated; use make first-boot PROFILE=$(ISD_PROFILE)"
	+@$(MAKE) first-boot PROFILE=$(ISD_PROFILE)

# '+' so nested make -C ISD inside bootstrap inherits the jobserver.
first-boot: $(IR0_KERNEL_ROOT_ISO)
	+@chmod +x "$(KERNEL_ROOT)/scripts/bootstrap-isd.sh"
	+@PROFILE="$(ISD_PROFILE)" \
		ROOT_FS="$(ISD_ROOT_FS)" \
		IR0_PRODUCT_PROFILE="$(ISD_PROFILE)" \
		IR0_ISD_ROOT="$(IR0_ISD_ROOT)" \
		IR0_ISD_URL="$(IR0_ISD_URL)" \
		IR0_USERSPACE_ROOT="$(IR0_ISD_ROOT)" \
		IR0_USERSPACE_URL="$(IR0_ISD_URL)" \
		ISD_ARCH="$(ISD_ARCH)" \
		"$(KERNEL_ROOT)/scripts/bootstrap-isd.sh"
	+@$(MAKE) -s machine-create PROFILE=$(ISD_PROFILE) ROOT_FS=$(ISD_ROOT_FS)
	@test -f "$(KERNEL_ROOT)/$(IR0_KERNEL_ROOT_ISO)" || { \
		echo "✗ bootstrap finished without $(IR0_KERNEL_ROOT_ISO)"; exit 1; \
	}
	@echo "  POWERON  make poweron PROFILE=$(ISD_PROFILE) ROOT_FS=$(ISD_ROOT_FS) IR0_MACHINE=$(IR0_MACHINE)"

machine-create: ensure-isd-root-disk ensure-isd-home
	@chmod +x "$(KERNEL_ROOT)/scripts/isd_machine_disk.sh"
	@IR0_MACHINE_BASE_DISK="$(IR0_ISD_ROOT_DISK)" \
		IR0_MACHINE_DISK="$(IR0_MACHINE_DISK)" \
		"$(KERNEL_ROOT)/scripts/isd_machine_disk.sh" create
	@if [ "$(ISD_REQUIRES_HOME_DISK)" = "1" ]; then \
		IR0_MACHINE_BASE_DISK="$(IR0_ISD_HOME_DISK)" \
		IR0_MACHINE_DISK="$(IR0_MACHINE_HOME_DISK)" \
		"$(KERNEL_ROOT)/scripts/isd_machine_disk.sh" create; \
	fi

machine-reset: ensure-isd-root-disk
	@chmod +x "$(KERNEL_ROOT)/scripts/isd_machine_disk.sh"
	@IR0_MACHINE_BASE_DISK="$(IR0_ISD_ROOT_DISK)" \
		IR0_MACHINE_DISK="$(IR0_MACHINE_DISK)" \
		CONFIRM_RESET="$(CONFIRM_RESET)" \
		"$(KERNEL_ROOT)/scripts/isd_machine_disk.sh" reset

machine-info:
	@echo "PROFILE       $(ISD_PROFILE)"
	@echo "MACHINE       $(IR0_MACHINE)"
	@echo "BASE DISK     $(IR0_ISD_ROOT_DISK) (ROOT_FS=$(ISD_ROOT_FS))"
	@echo "MACHINE DISK  $(IR0_MACHINE_DISK)"
	@echo "HOME DISK     $(IR0_MACHINE_HOME_DISK)"
	@echo "KERNEL        $$(python3 scripts/kernel_manager.py --machine-dir \
		"$(IR0_MACHINE_DIR)" --arch "$(ISD_ARCH)" \
		--profile "$(ISD_PROFILE)" --machine "$(IR0_MACHINE)" \
		resolve 2>/dev/null || echo "$(KERNEL_ROOT)/kernel-x64-userspace.iso")"
	@echo "VMWARE DISK   $(IR0_MACHINE_VMDK)"

kernel-manager-install: check-isd
	@if pgrep -f '^qemu-system-x86_64 .*$(IR0_MACHINE_DISK)' >/dev/null 2>&1; then \
		echo "✗ machine $(IR0_MACHINE) is running; power it off cleanly first"; \
		exit 2; \
	fi
	+@$(MAKE) -s kernel-x64-userspace.iso PROFILE=$(ISD_PROFILE)
	@python3 scripts/kernel_manager.py --machine-dir "$(IR0_MACHINE_DIR)" \
		--arch "$(ISD_ARCH)" --profile "$(ISD_PROFILE)" \
		--machine "$(IR0_MACHINE)" \
		--kernel-root "$(KERNEL_ROOT)" \
		--source "$(KERNEL_ROOT)/kernel-x64-userspace.iso" \
		--version "$(IR0_VERSION_STRING)" install-workspace

kernel-manager-list:
	@$(KMANG_PY) list
	@$(KMANG_PY) workspace
	@$(KMANG_PY) compare || true

kmang: check-isd
	@chmod +x scripts/kernel_manager.py
	@$(KMANG_PY) --make-arg "PROFILE=$(KMANG_PROFILE)" tui; \
	rc=$$?; \
	if [ $$rc -eq 10 ]; then \
		$(MAKE) poweron PROFILE=$(KMANG_PROFILE) IR0_MACHINE=$(KMANG_MACHINE); \
	elif [ $$rc -ne 0 ]; then \
		exit $$rc; \
	fi

tui-k: kmang

kmang-cli:
	@$(KMANG_PY) list --json

kmang-test:
	@python3 scripts/test_kernel_manager.py -v

usmang-test:
	@python3 scripts/test_userspace_manager.py -v

usmang: check-isd
	@chmod +x scripts/userspace_manager.py
	@$(USMANG_PY) summary

tui-u: check-isd
	@chmod +x scripts/userspace_manager.py
	@$(USMANG_PY) tui

usmang-verify: check-isd
	@chmod +x scripts/userspace_manager.py
	@$(USMANG_PY) verify

machine-update-kernel: check-isd
	@if pgrep -f '^qemu-system-x86_64 .*$(IR0_MACHINE_DISK)' >/dev/null 2>&1; then \
		echo "✗ machine $(IR0_MACHINE) is running; power it off cleanly first"; \
		exit 2; \
	fi
	+@$(MAKE) -s kernel-x64-userspace.iso
	@echo "✓ workspace ISO rebuilt: $(KERNEL_ROOT)/kernel-x64-userspace.iso"
	@echo "  DISK preserved: $(IR0_MACHINE_DISK)"
	@echo "  note: poweron still boots kmang Default until you enroll this ISO"
	@echo "        (make kmang → i, or make kernel-manager-install)"

machine-update-userspace: check-isd
	@if pgrep -f '^qemu-system-x86_64 .*$(IR0_MACHINE_DISK)' >/dev/null 2>&1; then \
		echo "✗ machine $(IR0_MACHINE) is running; power it off cleanly first"; \
		exit 2; \
	fi
	+@$(IR0_ISD_MAKE) update-machine MACHINE_DIR="$(IR0_MACHINE_DIR)"
	@mkdir -p "$(IR0_MACHINE_DIR)"
	@touch "$(IR0_MACHINE_DIR)/.desktop-sync-stamp"

ensure-machine-desktop-sync: check-isd
	@if [ "$(ISD_REQUIRES_HOME_DISK)" != "1" ]; then exit 0; fi
	@if [ ! -f "$(IR0_MACHINE_DISK)" ]; then exit 0; fi
	@if pgrep -f '^qemu-system-x86_64 .*$(IR0_MACHINE_DISK)' >/dev/null 2>&1; then \
		echo "note: machine running; skip desktop userspace sync"; exit 0; \
	fi
	@stamp="$(ISD_ROOTFS_STAMP)"; \
	sync_stamp="$(IR0_MACHINE_DIR)/.desktop-sync-stamp"; \
	if [ ! -f "$$sync_stamp" ] || [ "$$stamp" -nt "$$sync_stamp" ]; then \
		echo "  SYNC     ISD desktop rootfs → $(IR0_MACHINE_DISK)"; \
		$(MAKE) -s machine-update-userspace PROFILE=$(ISD_PROFILE) \
			IR0_MACHINE=$(IR0_MACHINE); \
	fi

machine-migrate-home: check-isd
	@if pgrep -f '^qemu-system-x86_64 .*$(IR0_MACHINE_DISK)' >/dev/null 2>&1; then \
		echo "✗ machine $(IR0_MACHINE) is running; power it off cleanly first"; exit 2; \
	fi
	@test -f "$(IR0_MACHINE_DISK)" -a -f "$(IR0_MACHINE_HOME_DISK)" || { \
		echo "✗ machine disks are missing"; exit 2; \
	}
	@if debugfs -R 'stat /ivan' "$(IR0_MACHINE_HOME_DISK)" 2>&1 | \
		grep -q '^Inode:'; then \
		echo "✗ ext2 /home/ivan already exists; refusing to merge over live data"; exit 2; \
	fi
	@cp --reflink=auto "$(IR0_MACHINE_HOME_DISK)" \
		"$(IR0_MACHINE_HOME_DISK).migration-new"
	@python3 scripts/migrate_minix_home_to_ext2.py \
		--minix "$(IR0_MACHINE_DISK)" \
		--ext2 "$(IR0_MACHINE_HOME_DISK).migration-new" \
		--kernel-root "$(KERNEL_ROOT)"
	@e2fsck -fn "$(IR0_MACHINE_HOME_DISK).migration-new"
	@cp --reflink=auto "$(IR0_MACHINE_HOME_DISK)" \
		"$(IR0_MACHINE_HOME_DISK).previous"
	@mv "$(IR0_MACHINE_HOME_DISK).migration-new" "$(IR0_MACHINE_HOME_DISK)"
	@echo "✓ legacy home migrated; rollback: $(IR0_MACHINE_HOME_DISK).previous"

image-vmware:
	@chmod +x "$(KERNEL_ROOT)/scripts/isd_machine_disk.sh"
	@IR0_MACHINE_BASE_DISK="$(IR0_ISD_DISK)" \
		IR0_MACHINE_DISK="$(IR0_MACHINE_DISK)" \
		IR0_MACHINE_VMDK="$(IR0_MACHINE_VMDK)" FORCE="$(FORCE)" \
		"$(KERNEL_ROOT)/scripts/isd_machine_disk.sh" export-vmdk
	@echo "  Attach $(KERNEL_ROOT)/kernel-x64-userspace.iso as the boot CD."

poweron: check-isd ensure-machine-desktop-sync
	@test -f "$(KERNEL_ROOT)/$(IR0_KERNEL_ROOT_ISO)" || { \
		echo "✗ missing $(KERNEL_ROOT)/$(IR0_KERNEL_ROOT_ISO)"; \
		echo "  Run make first-boot PROFILE=$(ISD_PROFILE) ROOT_FS=$(ISD_ROOT_FS) first."; \
		exit 2; \
	}
	@chmod +x "$(KERNEL_ROOT)/scripts/isd_machine_disk.sh"
	@IR0_MACHINE_BASE_DISK="$(IR0_ISD_ROOT_DISK)" \
		IR0_MACHINE_DISK="$(IR0_MACHINE_DISK)" \
		"$(KERNEL_ROOT)/scripts/isd_machine_disk.sh" create
	@echo "Running persistent IR0 + ISD machine ($(IR0_MACHINE)) ROOT_FS=$(ISD_ROOT_FS)"
	@echo "  DISK     $(IR0_MACHINE_DISK)"
	@KERNEL_ISO="$$(python3 scripts/kernel_manager.py --machine-dir \
		"$(IR0_MACHINE_DIR)" --arch "$(ISD_ARCH)" \
		--profile "$(ISD_PROFILE)" --machine "$(IR0_MACHINE)" \
		resolve 2>/dev/null)"; \
	if [ -z "$$KERNEL_ISO" ]; then \
		if find "$(IR0_MACHINE_DIR)/kernels" -maxdepth 1 -name '*.iso' \
			-print -quit 2>/dev/null | grep -q .; then \
			echo "✗ kmang has installed kernels but none verifies"; exit 2; \
		fi; \
		KERNEL_ISO="$(KERNEL_ROOT)/$(IR0_KERNEL_ROOT_ISO)"; \
	fi; \
	echo "  KERNEL   $$KERNEL_ISO"; \
	qemu-system-x86_64 -cdrom "$$KERNEL_ISO" \
		-drive "file=$(IR0_MACHINE_DISK),format=raw,if=ide,index=0" \
		$(IR0_MACHINE_HOME_QEMU_DRIVE) \
		$(QEMU_NET_ALL) $(QEMU_AUDIO_ALL) $(QEMU_SERIAL_COM1) $(QEMU_ISA_DEBUG_EXIT) \
		$(QEMU_DENNIS_9P) \
		-m 512M -no-reboot \
		$(QEMU_DISPLAY)

run-isd: kernel-x64-userspace.iso ensure-isd-disk ensure-isd-home
	@echo "Running IR0 + ISD (PROFILE=$(ISD_PROFILE))"
	@echo "  DISK     $(IR0_ISD_DISK)"
	@echo "  ISO      kernel-x64-userspace.iso"
	@echo "  Ungrab:  Ctrl+Alt+G"
	@echo "  Clipboard: make clip-send (host) → ir0-paste in guest (9p dennis)"
	@echo "  Guest:   first-boot wizard or login (development = autologin root)"
	qemu-system-x86_64 -cdrom kernel-x64-userspace.iso \
		-drive file=$(IR0_ISD_DISK),format=raw,if=ide,index=0 \
		$(IR0_ISD_HOME_QEMU_DRIVE) \
		$(QEMU_NET_ALL) $(QEMU_AUDIO_ALL) $(QEMU_SERIAL_COM1) $(QEMU_ISA_DEBUG_EXIT) \
		$(QEMU_DENNIS_9P) \
		-m 512M -no-reboot \
		$(QEMU_DISPLAY)

# Release / Tier 1.5: boot smoke on ISD-built disk (not legacy load-userspace-runit).
smoke-runit-boot-isd: kernel-x64-userspace.iso ensure-isd-disk
	@echo "  SMOKE   runit PID1 boot (ISD disk PROFILE=$(ISD_PROFILE))..."
	@DISK=$$(mktemp /tmp/ir0-runit-smoke.XXXXXX.img); \
	cp -f "$(IR0_ISD_DISK)" $$DISK; \
	$(SMOKE_QEMU_RUN) --log $(RUNIT_SMOKE_LOG) --timeout 50 --stale-sec 18 \
		--done RUNSV_CONSOLE_START --done RUNSV_LOGGER_START --done GETTY_READY -- \
		$(QEMU) -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK
	@if grep -q "RUNIT_STAGE1_OK" $(RUNIT_SMOKE_LOG) && \
	    grep -q "RUNIT_STAGE2_OK" $(RUNIT_SMOKE_LOG) && \
	    grep -q "RUNSV_CONSOLE_START" $(RUNIT_SMOKE_LOG) && \
	    grep -q "RUNSV_LOGGER_START" $(RUNIT_SMOKE_LOG) && \
	    grep -q "GETTY_READY" $(RUNIT_SMOKE_LOG) && \
	    grep -qE "FSCK_OK|FSCK_SKIPPED" $(RUNIT_SMOKE_LOG) && \
	    grep -qE "FIRSTBOOT_SKIP|FIRSTBOOT_OK|FIRSTBOOT_PENDING" $(RUNIT_SMOKE_LOG) && \
	    grep -q "DRIVER_SUMMARY_OK" $(RUNIT_SMOKE_LOG); then \
		echo "✓ smoke-runit-boot-isd passed"; \
	else \
		echo "✗ smoke-runit-boot-isd FAILED"; \
		grep -E 'RUNIT_|RUNSV_|GETTY_|FSCK_|FIRSTBOOT_|DRIVER_SUMMARY|KERNEL PANIC|panic' \
			$(RUNIT_SMOKE_LOG) | tail -40; \
		exit 1; \
	fi

# Generic init boot smoke (PROFILE=minimal|minimal-sysvinit|minimal-openrc).
smoke-init-boot-isd: kernel-x64-userspace.iso ensure-isd-disk
	@$(INIT_BOOT_PY) --profile $(ISD_PROFILE) --arch $(ISD_ARCH) --smoke-only

# Save serial boot capture under out/init-boot-capture/<arch>/<profile>/.
init-cap: kernel-x64-userspace.iso ensure-isd-disk
	@$(INIT_BOOT_PY) --profile $(ISD_PROFILE) --arch $(ISD_ARCH)

init-smoke: smoke-init-boot-isd

init-smoke-matrix: kernel-x64-userspace.iso
	@$(INIT_BOOT_PY) --matrix --arch $(ISD_ARCH)

# Dual-run lab: same ISD profiles × minix + ext2 root (review north star).
root-fs-smoke-matrix: kernel-x64-userspace.iso kernel-x64-ext2-root.iso
	@$(INIT_BOOT_PY) --matrix --root-fs-matrix --arch $(ISD_ARCH) --smoke-only

# STO-2: PID1 boot with kernel CONFIG_ROOT_FILESYSTEM=ext2 on ISD disk.ext2.img
smoke-ext2-root-boot: kernel-x64-ext2-root.iso
	@$(INIT_BOOT_PY) --profile minimal --root-fs ext2 --arch $(ISD_ARCH) --smoke-only

endif
