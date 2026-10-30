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

endif # _IR0_HOST_PORTABLE_MK
