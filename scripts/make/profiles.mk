# SPDX-License-Identifier: GPL-3.0-only
#
# IR0 product profiles — sacred Makefile entry helpers.
# Root Makefile stays thin; recipes live here.
#
#   make ir0_defconfig PROFILE=desktop
#   make ir0_defconfig PROFILE=hub BOARD=rpi4
#   make image
#   make hub-rpi4 | desktop-x86_64 | watch-rpi5-stub
#

PROFILE ?= desktop
BOARD ?=

IR0_CONFIGS_DIR := $(KERNEL_ROOT)/setup/configs

ifeq ($(PROFILE),desktop)
IR0_PROFILE_CONFIG := $(IR0_CONFIGS_DIR)/desktop-x86_64.defconfig
else ifeq ($(PROFILE),hub)
ifeq ($(BOARD),rpi4)
IR0_PROFILE_CONFIG := $(IR0_CONFIGS_DIR)/hub-rpi4.defconfig
else
IR0_PROFILE_CONFIG :=
endif
else ifeq ($(PROFILE),watch)
ifeq ($(BOARD),rpi5)
IR0_PROFILE_CONFIG := $(IR0_CONFIGS_DIR)/watch-rpi5.defconfig
else
IR0_PROFILE_CONFIG :=
endif
else
IR0_PROFILE_CONFIG :=
endif

.PHONY: ir0_defconfig image help-profiles \
	hub-rpi4 desktop-x86_64 watch-rpi5-stub mandocs-from-menuconfig

# Skip host gate: IR0_SKIP_DEPTEST=1 make hub-rpi4
IR0_SKIP_DEPTEST ?= 0

help-profiles:
	@echo "IR0 profiles / images (Makefile entry — see scripts/make/profiles.mk)"
	@echo "  PROFILE=$(PROFILE)  BOARD=$(BOARD)"
	@echo ""
	@echo "  make check-env | deptest             # host env diagnostic (desktop default)"
	@echo "  make deptest PROFILE=desktop-x86_64|userspace|hub-rpi4|watch|all"
	@echo "  make ir0_defconfig PROFILE=desktop"
	@echo "  make ir0_defconfig PROFILE=hub BOARD=rpi4"
	@echo "  make ir0_defconfig PROFILE=watch BOARD=rpi5"
	@echo "  make image                 # build artifact for current PROFILE/BOARD"
	@echo "  make desktop-x86_64        # deptest + defconfig + image (x86 ISO)"
	@echo "  make hub-rpi4              # deptest + defconfig + ARM64 rpi4 min"
	@echo "  make watch-rpi5-stub       # deptest + defconfig + ARM64 rpi5 stub"
	@echo ""
	@echo "  make sync-menuconfig       # merge new Kconfig symbols into .config"
	@echo "  make sync-menuconfig-defconfig"
	@echo "  make mandocs-from-menuconfig MANDOC_LANG=en   # batch mandocs (same as menuconfig M)"
	@echo ""
	@echo "Ceilings (honest):"
	@echo "  desktop-x86_64     Supported — QEMU ISO (make ir0 / make run)"
	@echo "  userspace          Experimental — musl/BusyBox ISO path"
	@echo "  hub-rpi4           Hardware lab / UART — boots under QEMU raspi4b when present;"
	@echo "                     not an SD-flashable appliance"
	@echo "  watch-rpi5-stub    Planned / compile stub — uart=none, no RP1 yet"

ir0_defconfig:
	@if [ -z "$(IR0_PROFILE_CONFIG)" ] || [ ! -f "$(IR0_PROFILE_CONFIG)" ]; then \
		echo "✗ ir0_defconfig: need valid PROFILE/BOARD (got PROFILE=$(PROFILE) BOARD=$(BOARD))"; \
		echo "  try: PROFILE=desktop | PROFILE=hub BOARD=rpi4 | PROFILE=watch BOARD=rpi5"; \
		exit 2; \
	fi
	@cp "$(IR0_PROFILE_CONFIG)" "$(KERNEL_ROOT)/.config"
	@python3 $(KERNEL_ROOT)/scripts/kconfig/menuconfig.py --sync >/dev/null
	@echo "✓ ir0_defconfig PROFILE=$(PROFILE) BOARD=$(BOARD) → .config (from $(notdir $(IR0_PROFILE_CONFIG)))"

image:
	@if [ "$(PROFILE)" = "desktop" ]; then \
		echo "  IMAGE   desktop-x86_64 → make ir0"; \
		$(MAKE) ir0; \
	elif [ "$(PROFILE)" = "hub" ] && [ "$(BOARD)" = "rpi4" ]; then \
		echo "  IMAGE   hub-rpi4 → kernel-arm64-rpi4-min.bin (UART/board min; appliance TBD)"; \
		$(MAKE) kernel-arm64-rpi4-min.bin arm64-rpi4-compile; \
	elif [ "$(PROFILE)" = "watch" ] && [ "$(BOARD)" = "rpi5" ]; then \
		echo "  IMAGE   watch-rpi5-stub → kernel-arm64-rpi5-min.bin (uart=none stub)"; \
		$(MAKE) kernel-arm64-rpi5-min.bin arm64-rpi5-compile; \
	else \
		echo "✗ image: unsupported PROFILE=$(PROFILE) BOARD=$(BOARD)"; \
		echo "  run: make help-profiles"; \
		exit 2; \
	fi

desktop-x86_64:
	@if [ "$(IR0_SKIP_DEPTEST)" != "1" ]; then \
		$(MAKE) deptest PROFILE=desktop BOARD=; \
	fi
	@$(MAKE) ir0_defconfig PROFILE=desktop BOARD=
	@$(MAKE) image PROFILE=desktop BOARD=

hub-rpi4:
	@if [ "$(IR0_SKIP_DEPTEST)" != "1" ]; then \
		$(MAKE) deptest PROFILE=hub BOARD=rpi4; \
	fi
	@$(MAKE) ir0_defconfig PROFILE=hub BOARD=rpi4
	@$(MAKE) image PROFILE=hub BOARD=rpi4

watch-rpi5-stub:
	@if [ "$(IR0_SKIP_DEPTEST)" != "1" ]; then \
		$(MAKE) deptest PROFILE=watch BOARD=rpi5; \
	fi
	@$(MAKE) ir0_defconfig PROFILE=watch BOARD=rpi5
	@$(MAKE) image PROFILE=watch BOARD=rpi5

# Batch mandocs (menuconfig --mandocs / UI key M).
# Use MANDOC_LANG=en|es|all (do not override env LANG — breaks locales).
MANDOC_LANG ?= en
mandocs-from-menuconfig:
	@python3 $(KERNEL_ROOT)/scripts/kconfig/menuconfig.py --mandocs $(MANDOC_LANG)
