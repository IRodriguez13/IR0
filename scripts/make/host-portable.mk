# SPDX-License-Identifier: GPL-3.0-only
#
# Portable host helpers shared by the IR0 Makefiles.
#
# Keep recipes working on Linux, BSD, and macOS. GNU-only flags belong behind
# these helpers so a portability regression has one place to fix. See
# Documentation/ai_driven_dev/rules/ir0-ktm-host-portability-safety.md.

ifndef _IR0_HOST_PORTABLE_MK
_IR0_HOST_PORTABLE_MK := 1

# Host temp directory. POSIX hosts may redirect TMPDIR to a per-user or
# sandboxed path, so never hardcode /tmp in a recipe. Resolve it once at
# parse time: recipes can then use $(HOST_TMP)/name without shell quoting
# surprises inside single-quoted strings.
HOST_TMP ?= $(patsubst %/,%,$(or $(TMPDIR),/tmp))

# GNU cp supports --reflink=auto (copy-on-write when the filesystem allows
# it, plain copy otherwise). BSD/macOS cp does not know the flag, so detect
# support once and fall back to a portable copy.
CP_REFLINK := $(shell \
	if cp --help 2>/dev/null | grep -q -- '--reflink'; then \
		echo 'cp --reflink=auto'; \
	else \
		echo cp; \
	fi)

endif # _IR0_HOST_PORTABLE_MK
