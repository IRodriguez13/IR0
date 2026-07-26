# SPDX-License-Identifier: GPL-3.0-only
# Extended QEMU run targets (networking, GDB, etc.) — not in default `make help`.
# Invoke: IR0_INCLUDE_QA=1 make run-tap   or   IR0_LEGACY_SMOKE=1 make run-tap

ifndef IR0_LEGACY_RUN_INCLUDED
IR0_LEGACY_RUN_INCLUDED := 1

.PHONY: run-debug run-nodisk run-console run-tap run-ping debug run-gdb

run-debug: kernel-x64.iso disk.img
	@echo "Running IR0 Kernel with debug output and all supported hardware..."
	@echo "Serial output will appear in this terminal"
	@$(QEMU) -cdrom kernel-x64.iso \
		$(QEMU_HW_IR0_ALL) \
		-m 512M -no-reboot -no-shutdown \
		$(QEMU_DISPLAY) \
		-monitor telnet:127.0.0.1:1234,server,nowait \
		-d guest_errors,int $(QEMU_LOG_FILE)

run-nodisk: kernel-x64.iso
	@echo "Running IR0 Kernel (no disk)..."
	@$(QEMU) -cdrom kernel-x64.iso \
		-m 512M -no-reboot -no-shutdown \
		-display gtk -serial stdio

run-console: kernel-x64.iso disk.img
	@echo "Running IR0 Kernel (console) with all supported hardware..."
	@$(QEMU) -cdrom kernel-x64.iso \
		$(QEMU_HW_IR0_ALL) \
		-m 512M -no-reboot -no-shutdown \
		$(QEMU_NGRAPHIC)

run-tap: kernel-x64.iso disk.img
	@echo "Running IR0 Kernel with TAP networking (requires root + bridge)..."
	@if [ ! -c /dev/net/tun ]; then \
		echo "ERROR: TUN/TAP device not available. Install: sudo modprobe tun"; \
		exit 1; \
	fi
	@$(MAKE) clean-net 2>/dev/null || true
	@$(MAKE) CFLAGS="$(CFLAGS) -DIR0_TAP_NETWORKING" ir0
	@sudo $(QEMU) -cdrom kernel-x64.iso \
		$(QEMU_NET_RTL8139_TAP) $(QEMU_STORAGE_IDE) $(QEMU_SERIAL_COM1) \
		-m 512M -no-reboot -no-shutdown \
		$(QEMU_DISPLAY) \
		$(QEMU_DEBUG_GUEST) $(QEMU_LOG_FILE)

run-ping: kernel-x64.iso disk.img
	@echo "  PING    Checking ICMP socket permissions..."
	@CURRENT=$$(cat /proc/sys/net/ipv4/ping_group_range 2>/dev/null); \
	if echo "$$CURRENT" | grep -q "^0[[:space:]]"; then \
		echo "          ping_group_range already configured: $$CURRENT"; \
	else \
		echo "          Setting net.ipv4.ping_group_range (requires sudo once)..."; \
		sudo sysctl -w net.ipv4.ping_group_range="0 2147483647" || exit 1; \
	fi
	@echo "  PING    Launching QEMU with user-mode networking (ICMP enabled)"
	@$(QEMU) -cdrom kernel-x64.iso \
		$(QEMU_HW_IR0_ALL) \
		-m 512M -no-reboot -no-shutdown \
		$(QEMU_DISPLAY)

debug: kernel-x64.iso disk.img
	@echo "Running IR0 Kernel (debug) with all supported hardware..."
	@$(QEMU) -cdrom kernel-x64.iso \
		$(QEMU_HW_IR0_ALL) \
		-m 512M -no-reboot -no-shutdown \
		$(QEMU_DISPLAY) \
		-d int,cpu_reset,guest_errors $(QEMU_LOG_FILE)

run-gdb: kernel-x64.iso disk.img
	@echo "  GDB     QEMU waiting for GDB on localhost:1234"
	@echo "          gdb -x scripts/kernel.gdb"
	@$(QEMU) -cdrom kernel-x64.iso \
		$(QEMU_SERIAL_COM1) -drive file=disk.img,format=raw,if=ide,index=0 \
		-m 512M -no-reboot -no-shutdown -display none -net none \
		-s -S

endif
