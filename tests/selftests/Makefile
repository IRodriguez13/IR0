# SPDX-License-Identifier: GPL-3.0-only
#
# Cross-arch IR0 userspace selftests. Built as static ET_EXEC programs and
# injected as /sbin/init under the existing bounded QEMU runners.

KERNEL_ROOT ?= $(abspath $(CURDIR)/../..)
ARCH ?= x86_64
BUILD_DIR := $(KERNEL_ROOT)/build/selftests/$(ARCH)
OPENAT_SRC := syscall/openat.c
OPENAT_BIN := $(BUILD_DIR)/openat
IDENTITY_SRC := syscall/identity.c
IDENTITY_BIN := $(BUILD_DIR)/identity
FAULT_SRC := mm/fault.c
FAULT_BIN := $(BUILD_DIR)/fault
PANIC_NESTED_SRC := kernel/panic_nested.c
PANIC_NESTED_BIN := $(BUILD_DIR)/panic_nested
FAULT_SIGNALS_SRC := kernel/fault_signals.c
FAULT_SIGNALS_BIN := $(BUILD_DIR)/fault_signals
SIGNAL_SEMANTICS_SRC := kernel/signal_semantics.c
SIGNAL_SEMANTICS_BIN := $(BUILD_DIR)/signal_semantics
SWAPON_SRC := syscall/swapon.c
SWAPON_BIN := $(BUILD_DIR)/swapon
KSFT_INC := -I$(CURDIR)

X86_CC ?= gcc
X86_MUSL_LIB ?= /usr/lib/x86_64-linux-musl
ARM64_MUSL_CC := $(KERNEL_ROOT)/toolchain/aarch64-linux-musl/bin/aarch64-linux-musl-gcc
ARM64_MUSL_CC_ALT := $(KERNEL_ROOT)/toolchain/aarch64-linux-musl-cross/bin/aarch64-linux-musl-gcc
ARM64_CC ?= $(shell \
	if test -x "$(ARM64_MUSL_CC)"; then echo "$(ARM64_MUSL_CC)"; \
	elif test -x "$(ARM64_MUSL_CC_ALT)"; then echo "$(ARM64_MUSL_CC_ALT)"; \
	else command -v aarch64-linux-musl-gcc 2>/dev/null; fi)

ifeq ($(ARCH),arm64)
CC := $(ARM64_CC)
CFLAGS := -static -fno-pie -no-pie -nostdlib -fno-stack-protector \
	-fno-asynchronous-unwind-tables \
	-Os -Wl,-Ttext-segment=0x60000000 -Wl,-e,_start \
	-Wl,-z,max-page-size=4096 \
	$(KSFT_INC) -DIR0_SELFTEST_NOLIBC \
	-DOPEN_EXISTING_PATH=\"/proc/uptime\"
else
CC := $(X86_CC)
CFLAGS := -static -fno-pie -no-pie -Os \
	-nostdinc -isystem /usr/include/x86_64-linux-musl \
	$(KSFT_INC) -DOPEN_EXISTING_PATH=\"/proc/uptime\"
LDLIBS := $(X86_MUSL_LIB)/crt1.o $(X86_MUSL_LIB)/crti.o \
	-nostdlib -L$(X86_MUSL_LIB) -lc $(X86_MUSL_LIB)/crtn.o
endif

.PHONY: all openat identity swapon fault panic_nested fault_signals signal_semantics clean

all: openat identity swapon fault panic_nested fault_signals signal_semantics

$(BUILD_DIR):
	@mkdir -p $@

openat: $(OPENAT_BIN)

$(OPENAT_BIN): $(OPENAT_SRC) ir0/ksft.h ir0/nolibc.h | $(BUILD_DIR)
	@if [ -z "$(CC)" ]; then \
		echo "tests/selftests: missing compiler for ARCH=$(ARCH)" >&2; \
		exit 1; \
	fi
	@echo "  CC      $(ARCH) selftest openat"
	@$(CC) $(CFLAGS) -o $@ $(OPENAT_SRC) $(LDLIBS)
	@file $@ | grep -qi 'statically linked'
	@test -x $@

identity: $(IDENTITY_BIN)

$(IDENTITY_BIN): $(IDENTITY_SRC) ir0/ksft.h ir0/nolibc.h | $(BUILD_DIR)
	@if [ -z "$(CC)" ]; then \
		echo "tests/selftests: missing compiler for ARCH=$(ARCH)" >&2; \
		exit 1; \
	fi
	@echo "  CC      $(ARCH) selftest identity"
	@$(CC) $(CFLAGS) -o $@ $(IDENTITY_SRC) $(LDLIBS)
	@file $@ | grep -qi 'statically linked'
	@test -x $@

swapon: $(SWAPON_BIN)

$(SWAPON_BIN): $(SWAPON_SRC) ir0/ksft.h ir0/nolibc.h | $(BUILD_DIR)
	@if [ -z "$(CC)" ]; then \
		echo "tests/selftests: missing compiler for ARCH=$(ARCH)" >&2; \
		exit 1; \
	fi
	@echo "  CC      $(ARCH) selftest swapon"
	@$(CC) $(CFLAGS) -o $@ $(SWAPON_SRC) $(LDLIBS)
	@file $@ | grep -qi 'statically linked'
	@test -x $@

signal_semantics: $(SIGNAL_SEMANTICS_BIN)

$(SIGNAL_SEMANTICS_BIN): $(SIGNAL_SEMANTICS_SRC) ir0/ksft.h ir0/nolibc.h | $(BUILD_DIR)
	@if [ -z "$(CC)" ]; then \
		echo "tests/selftests: missing compiler for ARCH=$(ARCH)" >&2; \
		exit 1; \
	fi
	@echo "  CC      $(ARCH) selftest signal_semantics"
	@$(CC) $(CFLAGS) -o $@ $(SIGNAL_SEMANTICS_SRC) $(LDLIBS)
	@file $@ | grep -qi 'statically linked'
	@test -x $@

fault_signals: $(FAULT_SIGNALS_BIN)

$(FAULT_SIGNALS_BIN): $(FAULT_SIGNALS_SRC) ir0/ksft.h ir0/nolibc.h | $(BUILD_DIR)
	@if [ -z "$(CC)" ]; then \
		echo "tests/selftests: missing compiler for ARCH=$(ARCH)" >&2; \
		exit 1; \
	fi
	@echo "  CC      $(ARCH) selftest fault_signals"
	@$(CC) $(CFLAGS) -o $@ $(FAULT_SIGNALS_SRC) $(LDLIBS)
	@file $@ | grep -qi 'statically linked'
	@test -x $@

panic_nested: $(PANIC_NESTED_BIN)

$(PANIC_NESTED_BIN): $(PANIC_NESTED_SRC) ir0/ksft.h ir0/nolibc.h | $(BUILD_DIR)
	@if [ -z "$(CC)" ]; then \
		echo "tests/selftests: missing compiler for ARCH=$(ARCH)" >&2; \
		exit 1; \
	fi
	@echo "  CC      $(ARCH) selftest panic_nested"
	@$(CC) $(CFLAGS) -o $@ $(PANIC_NESTED_SRC) $(LDLIBS)
	@file $@ | grep -qi 'statically linked'
	@test -x $@

fault: $(FAULT_BIN)

$(FAULT_BIN): $(FAULT_SRC) ir0/ksft.h ir0/nolibc.h | $(BUILD_DIR)
	@if [ -z "$(CC)" ]; then \
		echo "tests/selftests: missing compiler for ARCH=$(ARCH)" >&2; \
		exit 1; \
	fi
	@echo "  CC      $(ARCH) selftest fault"
	@$(CC) $(CFLAGS) -o $@ $(FAULT_SRC) $(LDLIBS)
	@file $@ | grep -qi 'statically linked'
	@test -x $@

clean:
	@rm -rf $(BUILD_DIR)
