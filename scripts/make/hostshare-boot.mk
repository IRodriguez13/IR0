# SPDX-License-Identifier: GPL-3.0-only
#
# Optional boot log dump to QEMU virtio-9p host share.
#
#   make run-bootlog
#   make smoke-boot-log-hostshare
#
# Requires CONFIG_BOOT_LOG_HOSTSHARE=y (set automatically by these targets).
# Root + /sbin/init still live on MINIX disk.img (required today); these
# targets reuse an existing disk.img when present and only rebuild the ISO
# with BOOT_LOG_HOSTSHARE=y — they do not re-inject the full rootfs unless
# disk.img is missing.
#

.PHONY: run-bootlog smoke-boot-log-hostshare help-bootlog

IR0_HOSTSHARE_DIR ?= $(KERNEL_ROOT)/build/hostshare
BOOT_LOG_HOSTSHARE_SMOKE_LOG ?= /tmp/ir0-boot-log-hostshare-smoke.log

help-bootlog:
	@echo "IR0 boot log → host (virtio-9p)"
	@echo "  make run-bootlog                 # QEMU + -virtfs → $(IR0_HOSTSHARE_DIR)/ir0-boot.log"
	@echo "  make smoke-boot-log-hostshare    # autokill gate BOOT_LOG_HOSTSHARE_OK"
	@echo "  Opt-in Kconfig: BOOT_LOG_HOSTSHARE (default n)"
	@echo "  Note: reuses disk.img when present (no needless reinject)"

# Desktop defconfig + ISO; reinject rootfs only if disk.img is missing.
define ir0_bootlog_prepare_desktop
	@$(MAKE) -s ir0_defconfig PROFILE=desktop BOARD=
	@python3 $(KERNEL_ROOT)/scripts/kconfig/menuconfig.py --set BOOT_LOG_HOSTSHARE=y
	@if [ ! -f $(KERNEL_ROOT)/disk.img ]; then \
		echo "  DISK    disk.img missing — load-userspace-runit (once)"; \
		$(MAKE) -s load-userspace-runit; \
	else \
		echo "  DISK    reusing existing disk.img (no reinject)"; \
	fi
	@$(MAKE) -s kernel-x64-userspace.iso
endef

# Enable symbol, rebuild ISO, boot with share. Interactive (no autokill).
run-bootlog:
	@mkdir -p "$(IR0_HOSTSHARE_DIR)"
	$(ir0_bootlog_prepare_desktop)
	@echo "  RUN     bootlog share=$(IR0_HOSTSHARE_DIR)"
	@echo "  After boot, host file: $(IR0_HOSTSHARE_DIR)/ir0-boot.log"
	qemu-system-x86_64 -cdrom kernel-x64-userspace.iso \
		-drive file=disk.img,format=raw,if=ide,index=0 \
		-fsdev local,id=ir0fs,path=$(IR0_HOSTSHARE_DIR),security_model=none \
		-device virtio-9p-pci,fsdev=ir0fs,mount_tag=ir0share,disable-modern=on \
		-m 512M -no-reboot -no-shutdown \
		-serial stdio -display none

smoke-boot-log-hostshare:
	@mkdir -p "$(IR0_HOSTSHARE_DIR)"
	@rm -f "$(IR0_HOSTSHARE_DIR)/ir0-boot.log"
	$(ir0_bootlog_prepare_desktop)
	@echo "  SMOKE   boot log → virtio-9p hostshare..."
	@rm -f $(BOOT_LOG_HOSTSHARE_SMOKE_LOG)
	@DISK=$$(mktemp /tmp/ir0-boot-log-hs.XXXXXX.img); \
	cp -f disk.img $$DISK; \
	$(SMOKE_QEMU_RUN) --log $(BOOT_LOG_HOSTSHARE_SMOKE_LOG) --timeout 45 --stale-sec 12 \
		--done BOOT_LOG_HOSTSHARE_OK -- \
		qemu-system-x86_64 -cdrom kernel-x64-userspace.iso \
		-drive file=$$DISK,format=raw,if=ide,index=0 \
		-fsdev local,id=ir0fs,path=$(IR0_HOSTSHARE_DIR),security_model=none \
		-device virtio-9p-pci,fsdev=ir0fs,mount_tag=ir0share,disable-modern=on \
		-serial stdio -display none -m 256M -no-reboot -net none; \
	rm -f $$DISK
	@if [ ! -f "$(IR0_HOSTSHARE_DIR)/ir0-boot.log" ]; then \
		echo "✗ smoke-boot-log-hostshare: missing host ir0-boot.log"; \
		grep -E 'BOOT_LOG_|HOSTSHARE_|panic' $(BOOT_LOG_HOSTSHARE_SMOKE_LOG) | tail -40; \
		exit 1; \
	fi
	@if ! grep -q 'IR0 kernel' "$(IR0_HOSTSHARE_DIR)/ir0-boot.log"; then \
		echo "✗ smoke-boot-log-hostshare: ir0-boot.log missing version banner"; \
		head -20 "$(IR0_HOSTSHARE_DIR)/ir0-boot.log"; \
		exit 1; \
	fi
	@python3 $(KERNEL_ROOT)/scripts/kconfig/menuconfig.py --set BOOT_LOG_HOSTSHARE=n
	@echo "✓ smoke-boot-log-hostshare passed (serial tag + host ir0-boot.log)"
