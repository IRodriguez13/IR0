# SPDX-License-Identifier: GPL-3.0-only
#
# IR0 product integration (machines, kmang, first-boot, QEMU product run).
# Included from scripts/make/isd.mk after bridge variables are defined.

ifndef _IR0_PRODUCT_MK
_IR0_PRODUCT_MK := 1

.PHONY: bootstrap-userspace first-boot machine-create machine-reset machine-info \
	kernel-manager-install kernel-manager-list kmang kmang-cli kmang-test \
	usmang usmang-test usmang-verify machine-update-kernel machine-update-userspace \
	ensure-machine-desktop-sync machine-migrate-home image-vmware poweron run-isd

# Deprecated alias → new bootstrap
bootstrap-userspace:
	@echo "note: bootstrap-userspace is deprecated; use make first-boot PROFILE=$(ISD_PROFILE)"
	+@$(MAKE) first-boot PROFILE=$(ISD_PROFILE)

# '+' so nested make -C ISD inside bootstrap inherits the jobserver.
first-boot: kernel-x64-userspace.iso
	+@chmod +x "$(KERNEL_ROOT)/scripts/bootstrap-isd.sh"
	+@PROFILE="$(ISD_PROFILE)" \
		IR0_PRODUCT_PROFILE="$(ISD_PROFILE)" \
		IR0_ISD_ROOT="$(IR0_ISD_ROOT)" \
		IR0_ISD_URL="$(IR0_ISD_URL)" \
		IR0_USERSPACE_ROOT="$(IR0_ISD_ROOT)" \
		IR0_USERSPACE_URL="$(IR0_ISD_URL)" \
		ISD_ARCH="$(ISD_ARCH)" \
		"$(KERNEL_ROOT)/scripts/bootstrap-isd.sh"
	+@$(MAKE) -s machine-create PROFILE=$(ISD_PROFILE) IR0_MACHINE=$(IR0_MACHINE)
	@test -f "$(KERNEL_ROOT)/kernel-x64-userspace.iso" || { \
		echo "✗ bootstrap finished without kernel-x64-userspace.iso"; exit 1; \
	}
	@echo "  POWERON  make poweron PROFILE=$(ISD_PROFILE) IR0_MACHINE=$(IR0_MACHINE)"

machine-create: ensure-isd-disk ensure-isd-home
	@chmod +x "$(KERNEL_ROOT)/scripts/isd_machine_disk.sh"
	@IR0_MACHINE_BASE_DISK="$(IR0_ISD_DISK)" \
		IR0_MACHINE_DISK="$(IR0_MACHINE_DISK)" \
		"$(KERNEL_ROOT)/scripts/isd_machine_disk.sh" create
	@if [ -n "$(ISD_PROFILE_IS_DESKTOP)" ]; then \
		IR0_MACHINE_BASE_DISK="$(IR0_ISD_HOME_DISK)" \
		IR0_MACHINE_DISK="$(IR0_MACHINE_HOME_DISK)" \
		"$(KERNEL_ROOT)/scripts/isd_machine_disk.sh" create; \
	fi

machine-reset: ensure-isd-disk
	@chmod +x "$(KERNEL_ROOT)/scripts/isd_machine_disk.sh"
	@IR0_MACHINE_BASE_DISK="$(IR0_ISD_DISK)" \
		IR0_MACHINE_DISK="$(IR0_MACHINE_DISK)" \
		CONFIRM_RESET="$(CONFIRM_RESET)" \
		"$(KERNEL_ROOT)/scripts/isd_machine_disk.sh" reset

machine-info:
	@echo "PROFILE       $(ISD_PROFILE)"
	@echo "MACHINE       $(IR0_MACHINE)"
	@echo "BASE DISK     $(IR0_ISD_DISK)"
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

kmang-cli:
	@$(KMANG_PY) list --json

kmang-test:
	@python3 scripts/test_kernel_manager.py -v

usmang-test:
	@python3 scripts/test_userspace_manager.py -v

usmang: check-isd
	@chmod +x scripts/userspace_manager.py
	@python3 scripts/userspace_manager.py --isd-root "$(IR0_ISD_ROOT)" \
		--profile "$(ISD_PROFILE)" --arch "$(ISD_ARCH)" summary
	@if [ -n "$(ISD_PROFILE_IS_DESKTOP)" ]; then \
		echo "---"; \
		python3 scripts/userspace_manager.py --isd-root "$(IR0_ISD_ROOT)" \
			--profile "$(ISD_PROFILE)" --arch "$(ISD_ARCH)" desktop; \
	fi
	@echo "guest: ir0-status version | packages | userland"

usmang-verify: check-isd
	@chmod +x scripts/userspace_manager.py
	@python3 scripts/userspace_manager.py --isd-root "$(IR0_ISD_ROOT)" \
		--profile "$(ISD_PROFILE)" --arch "$(ISD_ARCH)" verify

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
	@if [ -z "$(ISD_PROFILE_IS_DESKTOP)" ]; then \
		echo "✗ machine-update-userspace requires PROFILE=desktop or desktop-console"; exit 2; \
	fi
	@if pgrep -f '^qemu-system-x86_64 .*$(IR0_MACHINE_DISK)' >/dev/null 2>&1; then \
		echo "✗ machine $(IR0_MACHINE) is running; power it off cleanly first"; \
		exit 2; \
	fi
	+@$(IR0_ISD_MAKE) rootfs-tree
	@chmod +x scripts/isd_machine_desktop_update.sh
	@IR0_ISD_ROOTFS="$(IR0_ISD_ROOTFS)" \
		IR0_MACHINE_DISK="$(IR0_MACHINE_DISK)" \
		IR0_INJECT_TOOL="$(KERNEL_ROOT)/scripts/inject_init_minix.py" \
		scripts/isd_machine_desktop_update.sh
	@mkdir -p "$(IR0_MACHINE_DIR)"
	@touch "$(IR0_MACHINE_DIR)/.desktop-sync-stamp"

ensure-machine-desktop-sync: check-isd
	@if [ -z "$(ISD_PROFILE_IS_DESKTOP)" ]; then exit 0; fi
	@if [ ! -f "$(IR0_MACHINE_DISK)" ]; then exit 0; fi
	@if pgrep -f '^qemu-system-x86_64 .*$(IR0_MACHINE_DISK)' >/dev/null 2>&1; then \
		echo "note: machine running; skip desktop userspace sync"; exit 0; \
	fi
	@stamp="$(IR0_ISD_ROOT)/out/$(ISD_ARCH)/stamps/rootfs/$(ISD_PROFILE)"; \
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
	@test -f "$(KERNEL_ROOT)/kernel-x64-userspace.iso" || { \
		echo "✗ missing $(KERNEL_ROOT)/kernel-x64-userspace.iso"; \
		echo "  Run make first-boot PROFILE=$(ISD_PROFILE) first."; \
		exit 2; \
	}
	@chmod +x "$(KERNEL_ROOT)/scripts/isd_machine_disk.sh"
	@IR0_MACHINE_BASE_DISK="$(IR0_ISD_DISK)" \
		IR0_MACHINE_DISK="$(IR0_MACHINE_DISK)" \
		"$(KERNEL_ROOT)/scripts/isd_machine_disk.sh" create
	@echo "Running persistent IR0 + ISD machine ($(IR0_MACHINE))"
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
		KERNEL_ISO="$(KERNEL_ROOT)/kernel-x64-userspace.iso"; \
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

endif
