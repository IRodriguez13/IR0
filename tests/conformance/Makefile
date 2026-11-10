# SPDX-License-Identifier: GPL-3.0-only
#
# IR0 syscall conformance runner and bounded syscall fuzzer. Both are static
# ET_EXEC programs injected as /sbin/init under the bounded QEMU runner
# (scripts/ir0_conformance.py). x86_64 links musl; arm64 links no libc.
#
#   make -C tests/conformance ARCH=x86_64|arm64 conformance
#   make -C tests/conformance ARCH=x86_64|arm64 fuzz SEED=1 COUNT=2000

KERNEL_ROOT ?= $(abspath $(CURDIR)/../..)
ARCH ?= x86_64
BUILD_DIR := $(KERNEL_ROOT)/build/conformance/$(ARCH)
SEED ?= 1
COUNT ?= 2000
BATCH ?= 0
TRACE ?= 0
FUZZ_MAX_SEC ?= 120

RT_SRCS := rt.c
RT_HDRS := rt.h
CONF_SRCS := conformance.c cases_fs.c cases_proc.c cases_mm.c cases_sock.c
CONF_HDRS := conf.h cases.h $(RT_HDRS)
CONF_BIN := $(BUILD_DIR)/conformance
FUZZ_BIN := $(BUILD_DIR)/fuzz-s$(SEED)-n$(COUNT)-b$(BATCH)-t$(TRACE)

X86_CC ?= gcc
X86_MUSL_LIB ?= /usr/lib/x86_64-linux-musl
include $(KERNEL_ROOT)/scripts/make/worktree.mk
ARM64_MUSL_CC := $(IR0_TOOLCHAIN_DIR)/aarch64-linux-musl/bin/aarch64-linux-musl-gcc
ARM64_MUSL_CC_ALT := $(IR0_TOOLCHAIN_DIR)/aarch64-linux-musl-cross/bin/aarch64-linux-musl-gcc
ARM64_CC ?= $(shell \
	if test -x "$(ARM64_MUSL_CC)"; then echo "$(ARM64_MUSL_CC)"; \
	elif test -x "$(ARM64_MUSL_CC_ALT)"; then echo "$(ARM64_MUSL_CC_ALT)"; \
	else command -v aarch64-linux-musl-gcc 2>/dev/null; fi)

COMMON_CFLAGS := -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter \
	-Wno-unused-but-set-variable \
	-fno-stack-protector -fno-builtin -fno-asynchronous-unwind-tables \
	-D_GNU_SOURCE -I$(CURDIR)

ifeq ($(ARCH),arm64)
CC := $(ARM64_CC)
# musl headers give the aarch64 constants and layouts; nothing links libc.
CFLAGS := $(COMMON_CFLAGS) -static -fno-pie -no-pie -nostdlib -Os -DCONF_TERSE \
	-ffunction-sections -fdata-sections -Wl,--gc-sections \
	-Wl,-Ttext-segment=0x60000000 -Wl,-e,_start -Wl,-z,max-page-size=4096 -s
LDLIBS := -lgcc
else
CC := $(X86_CC)
CFLAGS := $(COMMON_CFLAGS) -static -fno-pie -no-pie -Os \
	-nostdinc -isystem /usr/include/x86_64-linux-musl
LDLIBS := $(X86_MUSL_LIB)/crt1.o $(X86_MUSL_LIB)/crti.o \
	-nostdlib -L$(X86_MUSL_LIB) -lc -lgcc $(X86_MUSL_LIB)/crtn.o
endif

FUZZ_DEFS := -DFUZZ_SEED=$(SEED)UL -DFUZZ_COUNT=$(COUNT)L \
	-DFUZZ_BATCH=$(BATCH)L -DFUZZ_TRACE=$(TRACE) -DFUZZ_MAX_SEC=$(FUZZ_MAX_SEC)L

# Red selftests (tests/selftests/kernel/linux_abi_*.c) reuse this runner with
# their own case list: make ARCH=... red RED=linux_abi_fs
RED ?= linux_abi_fs
RED_DIR := $(KERNEL_ROOT)/tests/selftests/kernel
RED_BIN := $(KERNEL_ROOT)/build/selftests/$(ARCH)/$(RED)

STUB_BIN := $(BUILD_DIR)/init-stub

.PHONY: all conformance fuzz red stub print-fuzz-bin clean

all: conformance fuzz

$(BUILD_DIR):
	@mkdir -p $@

define need_cc
	@if [ -z "$(CC)" ]; then \
		echo "tests/conformance: missing compiler for ARCH=$(ARCH)" \
			"(run scripts/setup_musl_aarch64.sh for arm64)" >&2; \
		exit 1; \
	fi
endef

conformance: $(CONF_BIN)

$(CONF_BIN): $(CONF_SRCS) $(RT_SRCS) $(CONF_HDRS) Makefile | $(BUILD_DIR)
	$(need_cc)
	@echo "  CC      $(ARCH) conformance"
	@$(CC) $(CFLAGS) -o $@ $(CONF_SRCS) $(RT_SRCS) $(LDLIBS)
	@file $@ | grep -qi 'statically linked'

fuzz: $(FUZZ_BIN)

print-fuzz-bin:
	@echo $(FUZZ_BIN)

$(FUZZ_BIN): fuzz.c $(RT_SRCS) $(RT_HDRS) Makefile | $(BUILD_DIR)
	$(need_cc)
	@echo "  CC      $(ARCH) fuzz seed=$(SEED) count=$(COUNT)"
	@$(CC) $(CFLAGS) $(FUZZ_DEFS) -o $@ fuzz.c $(RT_SRCS) $(LDLIBS)
	@file $@ | grep -qi 'statically linked'

stub: $(STUB_BIN)

$(STUB_BIN): init_stub.c Makefile | $(BUILD_DIR)
	$(need_cc)
	@$(CC) $(CFLAGS) -o $@ init_stub.c

red: $(RED_BIN)

$(RED_BIN): $(RED_DIR)/$(RED).c $(RED_DIR)/$(RED).h conformance.c $(RT_SRCS) \
		conf.h $(RT_HDRS) Makefile
	$(need_cc)
	@mkdir -p $(dir $@)
	@echo "  CC      $(ARCH) red selftest $(RED)"
	@$(CC) $(CFLAGS) -I$(RED_DIR) -DCONF_CASE_LIST='"$(RED).h"' -o $@ \
		conformance.c $(RED_DIR)/$(RED).c $(RT_SRCS) $(LDLIBS)
	@file $@ | grep -qi 'statically linked'

clean:
	@rm -rf $(BUILD_DIR)
