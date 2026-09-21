# SPDX-License-Identifier: GPL-3.0-only
#
# Public IR0 build/tooling entry points for external distros (ISD, etc.).
# Paths and targets listed in IR0_ISD_INTERFACE (PUBLIC_TARGET=...).

ifndef _IR0_PUBLIC_MK
_IR0_PUBLIC_MK := 1

.PHONY: ir0-minix-inject-path ir0-verify-minix-path ir0-public-check

ir0-minix-inject-path:
	@test -f "$(KERNEL_ROOT)/scripts/inject_init_minix.py" || { \
		echo "✗ missing scripts/inject_init_minix.py" >&2; exit 1; \
	}
	@echo "$(KERNEL_ROOT)/scripts/inject_init_minix.py"

ir0-verify-minix-path:
	@test -f "$(KERNEL_ROOT)/scripts/verify_minix_rootfs.py" || { \
		echo "✗ missing scripts/verify_minix_rootfs.py" >&2; exit 1; \
	}
	@echo "$(KERNEL_ROOT)/scripts/verify_minix_rootfs.py"

ir0-public-check: ir0-minix-inject-path ir0-verify-minix-path headers_install kernel-x64-userspace.iso
	@echo "✓ ir0-public-check OK"

endif
