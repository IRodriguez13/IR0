# SPDX-License-Identifier: GPL-3.0-only
#
# ISD bridge — product distribution lives in the sibling ISD repo.
# Included from the IR0 Makefile (index only; recipes stay here).
#
# Public targets:
#   check-isd clone-isd isd-defconfig isdconfig
#   isd isd-rootfs isd-image isd-clean first-boot
#
# Canonical interface: PROFILE passed to ISD (see ISD profiles/; no kernel-side semantics).
# Compat: IR0_PRODUCT_PROFILE, IR0_USERSPACE_ROOT/URL, bootstrap-userspace

ifndef _IR0_ISD_MK
_IR0_ISD_MK := 1

# --- paths / URLs ------------------------------------------------------------

# Defaults (origin → file when unset). CLI/env IR0_ISD_* already win over ?=.
IR0_ISD_ROOT ?= $(abspath $(KERNEL_ROOT)/../ISD)
IR0_ISD_URL  ?= https://github.com/IRodriguez13/ISD.git

# Capture before we rewrite aliases (for deprecation notes).
_IR0_USERSPACE_ROOT_ORIGIN := $(origin IR0_USERSPACE_ROOT)
_IR0_USERSPACE_URL_ORIGIN := $(origin IR0_USERSPACE_URL)

# Deprecated IR0_USERSPACE_* aliases — keep in sync for CLI and environment.
# Priority: CLI IR0_ISD_* > CLI IR0_USERSPACE_* > env IR0_ISD_* >
#           env IR0_USERSPACE_* > file default above.
ifeq ($(origin IR0_USERSPACE_ROOT),command line)
  ifneq ($(origin IR0_ISD_ROOT),command line)
    IR0_ISD_ROOT := $(IR0_USERSPACE_ROOT)
  endif
else ifeq ($(origin IR0_USERSPACE_ROOT),environment)
  ifeq ($(origin IR0_ISD_ROOT),file)
    IR0_ISD_ROOT := $(IR0_USERSPACE_ROOT)
  endif
endif
ifeq ($(origin IR0_USERSPACE_URL),command line)
  ifneq ($(origin IR0_ISD_URL),command line)
    IR0_ISD_URL := $(IR0_USERSPACE_URL)
  endif
else ifeq ($(origin IR0_USERSPACE_URL),environment)
  ifeq ($(origin IR0_ISD_URL),file)
    IR0_ISD_URL := $(IR0_USERSPACE_URL)
  endif
endif
# Legacy recipes still read IR0_USERSPACE_*; always alias to the ISD path.
IR0_USERSPACE_ROOT := $(IR0_ISD_ROOT)
IR0_USERSPACE_URL := $(IR0_ISD_URL)

ISD_ARCH ?= x86_64

# Resolve ISD product profile without clobbering kernel deptest PROFILE defaults.
# When PROFILE is a known ISD profile name (CLI or env), map it to ISD_PROFILE.
# Deptest uses other PROFILE values (e.g. arch labels) — those are ignored here.
ISD_PROFILE := minimal
ifdef IR0_PRODUCT_PROFILE
  ISD_PROFILE := $(IR0_PRODUCT_PROFILE)
endif
ifneq ($(filter minimal development desktop desktop-console appliance,$(PROFILE)),)
  ISD_PROFILE := $(PROFILE)
endif
# Keep IR0_PRODUCT_PROFILE in sync for scripts that still read it.
IR0_PRODUCT_PROFILE := $(ISD_PROFILE)
export IR0_PRODUCT_PROFILE

IR0_ISD_MAKE = $(MAKE) -C "$(IR0_ISD_ROOT)" \
	IR0_ROOT="$(KERNEL_ROOT)" \
	ARCH="$(ISD_ARCH)" \
	PROFILE="$(ISD_PROFILE)"

# Artifact paths come from ISD print-artifacts-mk (cached .mk; not hardcoded out/ layout).
IR0_ISD_DISK ?=
IR0_ISD_HOME_DISK ?=
IR0_ISD_ROOTFS ?=
ISD_REQUIRES_HOME_DISK ?=
ISD_VARIANT_ID ?=
ISD_ROOTFS_STAMP ?=
ISD_ARTIFACTS_CACHE := $(KERNEL_ROOT)/.cache/isd-artifacts/$(ISD_ARCH)-$(ISD_PROFILE).mk
KMANG_ARTIFACTS_CACHE := $(KERNEL_ROOT)/.cache/isd-artifacts/kmang-$(KMANG_ARCH)-$(KMANG_PROFILE).mk
ifneq ($(wildcard $(IR0_ISD_ROOT)/Makefile),)
$(shell bash "$(KERNEL_ROOT)/scripts/load_isd_artifacts.sh" \
	"$(IR0_ISD_ROOT)" "$(ISD_ARCH)" "$(ISD_PROFILE)" "$(KERNEL_ROOT)" \
	"$(ISD_ARTIFACTS_CACHE)")
$(shell bash "$(KERNEL_ROOT)/scripts/load_isd_artifacts.sh" \
	"$(IR0_ISD_ROOT)" "$(KMANG_ARCH)" "$(KMANG_PROFILE)" "$(KERNEL_ROOT)" \
	"$(KMANG_ARTIFACTS_CACHE)" KMANG_)
-include $(ISD_ARTIFACTS_CACHE)
-include $(KMANG_ARTIFACTS_CACHE)
endif
KMANG_ISD_DISK ?= $(IR0_ISD_DISK)

# Mutable guest state lives outside ISD out/. Rebuilding a package or rootfs may
# recreate IR0_ISD_DISK, but must never overwrite an installed machine.
IR0_MACHINE ?= default
IR0_MACHINE_ROOT ?= $(abspath $(IR0_ISD_ROOT)/../IR0-machines)
IR0_MACHINE_DIR = $(IR0_MACHINE_ROOT)/$(ISD_ARCH)/$(ISD_PROFILE)/$(IR0_MACHINE)
IR0_MACHINE_DISK = $(IR0_MACHINE_DIR)/disk.img
IR0_MACHINE_HOME_DISK = $(IR0_MACHINE_DIR)/home.ext2.img
IR0_MACHINE_VMDK = $(IR0_MACHINE_DIR)/ir0-$(ISD_PROFILE).vmdk
IR0_MACHINE_KERNEL_LINK = $(IR0_MACHINE_DIR)/kernel-current.iso
IR0_KERNEL_BUILD_ID = $(IR0_VERSION_STRING)-build$(IR0_BUILD_NUMBER)

# Canonical kernel-manager context. `make kmang` always opens the same store;
# product build PROFILE variables cannot silently redirect its history.
KMANG_ARCH ?= x86_64
KMANG_PROFILE ?= desktop
KMANG_MACHINE ?= default
KMANG_MACHINE_DIR = $(IR0_MACHINE_ROOT)/$(KMANG_ARCH)/$(KMANG_PROFILE)/$(KMANG_MACHINE)

# Keep comma-bearing QEMU arguments out of $(if ...): make treats their commas
# as function separators even when the text is shell-quoted.
# Attach optional home disk when ISD declares REQUIRES_HOME_DISK=1.
ifeq ($(ISD_REQUIRES_HOME_DISK),1)
IR0_MACHINE_HOME_QEMU_DRIVE = -drive "file=$(IR0_MACHINE_HOME_DISK),format=raw,if=ide,index=1"
IR0_ISD_HOME_QEMU_DRIVE = -drive "file=$(IR0_ISD_HOME_DISK),format=raw,if=ide,index=1"
endif

IR0_USERSPACE_OUT = $(IR0_ISD_ROOT)/out
IR0_USERSPACE_MAKE = $(MAKE) -s -C $(IR0_ISD_ROOT) IR0_ROOT=$(KERNEL_ROOT) ARCH=$(ISD_ARCH)

.PHONY: check-isd clone-isd isd-defconfig isdconfig isd isd-rootfs isd-image \
	isd-clean check-userspace warn-userspace-deprecated ensure-isd-disk \
	ensure-isd-home isd-contracts

warn-userspace-deprecated:
	@case "$(_IR0_USERSPACE_ROOT_ORIGIN)" in \
		command\ line|environment) \
			echo "note: IR0_USERSPACE_ROOT is deprecated; use IR0_ISD_ROOT=$(IR0_ISD_ROOT)" ;; \
	esac
	@case "$(_IR0_USERSPACE_URL_ORIGIN)" in \
		command\ line|environment) \
			echo "note: IR0_USERSPACE_URL is deprecated; use IR0_ISD_URL=$(IR0_ISD_URL)" ;; \
	esac

check-isd:
	@if [ ! -f "$(IR0_ISD_ROOT)/Makefile" ]; then \
		echo "✗ ISD not found at $(IR0_ISD_ROOT)"; \
		echo "  First time:  make first-boot PROFILE=$(ISD_PROFILE)"; \
		echo "  Or clone:    make clone-isd"; \
		echo "  Or set:      IR0_ISD_ROOT=/path/to/ISD"; \
		exit 1; \
	fi
	@case "$(ISD_ARCH)" in \
		x86_64|x86-64) ;; \
		*) echo "✗ first-boot/ISD product path currently supports ISD_ARCH=x86_64 (got $(ISD_ARCH))"; exit 1 ;; \
	esac
	@if [ ! -f "$(IR0_ISD_ROOT)/IR0_ISD_INTERFACE_SUPPORTED" ]; then \
		echo "✗ ISD missing IR0_ISD_INTERFACE_SUPPORTED at $(IR0_ISD_ROOT)"; \
		exit 1; \
	fi
	@kernel_ver=$$(grep '^VERSION=' "$(KERNEL_ROOT)/IR0_ISD_INTERFACE" 2>/dev/null | cut -d= -f2); \
	supported=$$(tr -d '[:space:]' < "$(IR0_ISD_ROOT)/IR0_ISD_INTERFACE_SUPPORTED"); \
	if [ -z "$$kernel_ver" ]; then \
		echo "✗ missing $(KERNEL_ROOT)/IR0_ISD_INTERFACE"; exit 1; \
	fi; \
	if [ "$$kernel_ver" -gt "$$supported" ] 2>/dev/null; then \
		echo "✗ IR0/ISD interface mismatch: kernel=$$kernel_ver ISD supports<=$$supported"; \
		exit 1; \
	fi; \
	echo "  ISD      $(IR0_ISD_ROOT) (interface $$kernel_ver/<=$$supported)"

clone-isd:
	@if [ -f "$(IR0_ISD_ROOT)/Makefile" ]; then \
		echo "  CLONE    ISD already present at $(IR0_ISD_ROOT)"; \
	else \
		parent="$(dir $(IR0_ISD_ROOT))"; \
		mkdir -p "$$parent"; \
		echo "  CLONE    $(IR0_ISD_URL) → $(IR0_ISD_ROOT)"; \
		git clone --depth 1 "$(IR0_ISD_URL)" "$(IR0_ISD_ROOT)"; \
	fi

isd-defconfig: check-isd
	+@$(IR0_ISD_MAKE) isd-defconfig

# '+' forwards the jobserver; keep the caller's TTY for the interactive menu.
isdconfig: check-isd
	+@$(IR0_ISD_MAKE) isdconfig

isd: check-isd
	+@$(IR0_ISD_MAKE) build

isd-rootfs: check-isd
	+@$(IR0_ISD_MAKE) rootfs-tree

isd-image: check-isd
	+@$(IR0_ISD_MAKE) image-minix
	@echo "✓ isd-image $(IR0_ISD_DISK)"

isd-clean: check-isd
	+@$(IR0_ISD_MAKE) clean

# Alias: old check-userspace name
check-userspace: check-isd

# Ensure per-PROFILE disk is up to date with stamps / .isdconfig.
# Always runs ISD image-minix (incremental): rebuilds when config or packages change.
ensure-isd-disk: check-isd
	@echo "ISD disk    PROFILE=$(ISD_PROFILE) → $(IR0_ISD_DISK)"
	@echo "            (first pack or format-large can take 1–3 min; stamps skip work when clean)"
	+@$(IR0_ISD_MAKE) fetch
	+@$(IR0_ISD_MAKE) image-minix
	@test -f "$(IR0_ISD_DISK)" || { \
		echo "✗ failed to create $(IR0_ISD_DISK)"; \
		exit 1; \
	}
	@echo "  DISK     $(IR0_ISD_DISK)"

ensure-isd-home: check-isd
	+@if [ "$(ISD_REQUIRES_HOME_DISK)" = "1" ]; then \
		$(IR0_ISD_MAKE) image-ext2-home; \
		test -f "$(IR0_ISD_HOME_DISK)"; \
	fi

# Host contract tests for ISD bridge + ensure-host-deps (no sudo, no QEMU).
isd-contracts:
	@chmod +x scripts/test-isd-contracts.sh
	@scripts/test-isd-contracts.sh

# Shared kmang CLI flags: store path + provenance context for local builds.
KMANG_PY = python3 scripts/kernel_manager.py \
	--machine-dir "$(KMANG_MACHINE_DIR)" \
	--arch "$(KMANG_ARCH)" --profile "$(KMANG_PROFILE)" \
	--machine "$(KMANG_MACHINE)" \
	--kernel-root "$(KERNEL_ROOT)" \
	--isd-root "$(IR0_ISD_ROOT)" \
	--isd-disk "$(KMANG_ISD_DISK)" \
	--machine-disk "$(KMANG_MACHINE_DIR)/disk.img" \
	--source "$(KERNEL_ROOT)/kernel-x64-userspace.iso" \
	--version "$(IR0_VERSION_STRING)"

-include $(KERNEL_ROOT)/scripts/make/product.mk

endif
