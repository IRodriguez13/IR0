# SPDX-License-Identifier: GPL-3.0-only
# Daily build/run targets — included from the main Makefile.
#
# Variables:
#   IR0_DISK         disk image (default: disk.img)
#   IR0_PID1 / INIT  PID1 binary injected as /sbin/init (run-pid1)
#   IR0_FRESH_DISK=1 copy disk to temp before inject (run-pid1)
#   IR0_QEMU_ARGS    extra QEMU arguments
#   IR0_DEBUG=1      serial stdio + no GUI (run / run-dbgshell / run-pid1)
#   IR0_SERIAL_LOG   serial log file when not using stdio (default: /tmp/ir0-run.log)

IR0_DISK ?= disk.img
IR0_PID1 ?= setup/pid1/sbin/init
IR0_SERIAL_LOG ?= /tmp/ir0-run.log
IR0_QEMU_RUN ?= $(QEMU)

.PHONY: run run-dbgshell run-pid1

# Default daily dev: dbgshell kernel ISO (CONFIG_KERNEL_DEBUG_SHELL=y).
run: kernel-x64.iso
	@echo "Running IR0 (dbgshell kernel — daily dev)"
	@if [ "$(IR0_DEBUG)" = "1" ]; then \
		$(IR0_QEMU_RUN) -cdrom kernel-x64.iso \
			$(QEMU_HW_IR0_ALL) \
			-m 512M -no-reboot -no-shutdown \
			-serial stdio -display none -net none \
			$(IR0_QEMU_ARGS); \
	else \
		QEMU_LOG_OPTION=""; \
		if [ -f qemu_debug.log ] && [ ! -w qemu_debug.log ]; then \
			echo "  NOTE  qemu_debug.log not writable — skipping guest log file"; \
		elif touch qemu_debug.log 2>/dev/null; then \
			QEMU_LOG_OPTION="$(QEMU_LOG_FILE)"; \
			rm -f qemu_debug.log; \
		fi; \
		$(IR0_QEMU_RUN) -cdrom kernel-x64.iso \
			$(QEMU_HW_IR0_ALL) \
			-m 512M -no-reboot -no-shutdown \
			$(QEMU_DISPLAY) \
			$(QEMU_DEBUG_GUEST) $$QEMU_LOG_OPTION \
			$(IR0_QEMU_ARGS); \
	fi

# Console dbgshell — serial only, no smokes.
run-dbgshell: kernel-x64.iso
	@echo "Running IR0 dbgshell (serial stdio)"
	@$(IR0_QEMU_RUN) -cdrom kernel-x64.iso \
		$(QEMU_HW_IR0_ALL) \
		-m 512M -no-reboot -no-shutdown \
		-serial stdio -display none -net none \
		$(IR0_QEMU_ARGS)

# Userspace PID1: kernel-x64-userspace.iso + injected /sbin/init.
# Does not rebuild BusyBox or run tests. INIT= overrides IR0_PID1.
run-pid1: kernel-x64-userspace.iso
	@INIT="$(or $(INIT),$(IR0_PID1))" \
		IR0_DISK="$(IR0_DISK)" \
		IR0_FRESH_DISK="$(IR0_FRESH_DISK)" \
		IR0_DEBUG="$(IR0_DEBUG)" \
		IR0_SERIAL_LOG="$(IR0_SERIAL_LOG)" \
		IR0_QEMU_ARGS="$(IR0_QEMU_ARGS)" \
		QEMU="$(IR0_QEMU_RUN)" \
		bash "$(KERNEL_ROOT)/scripts/run_pid1.sh"
