# SPDX-License-Identifier: GPL-3.0-only
#
# Locate untracked inputs from a linked git worktree.
#
# toolchain/ and the sibling ../ISD checkout are not tracked, so a linked
# worktree (git worktree add ../IR0-wt/<topic>) has neither. Each lookup tries
# the tree itself first, then the main checkout that owns the git common dir.
# Requires KERNEL_ROOT to be set by the including Makefile.

ifndef _IR0_WORKTREE_MK
_IR0_WORKTREE_MK := 1

# Main checkout. Equals KERNEL_ROOT outside a linked worktree, and also when
# git is unavailable (release tarball).
IR0_MAIN_ROOT := $(abspath $(dir $(shell git -C $(KERNEL_ROOT) rev-parse --path-format=absolute --git-common-dir 2>/dev/null)))
ifeq ($(IR0_MAIN_ROOT),)
IR0_MAIN_ROOT := $(abspath $(KERNEL_ROOT))
endif

# ir0_local_path: first existing path of $(1) under KERNEL_ROOT, then under
# the main checkout; KERNEL_ROOT's spelling when neither exists, so error
# messages name the path the caller would populate.
ir0_local_path = $(abspath $(firstword $(wildcard $(KERNEL_ROOT)/$(1) $(IR0_MAIN_ROOT)/$(1)) $(KERNEL_ROOT)/$(1)))

# Repo-local cross toolchain (scripts/setup_musl_aarch64.sh).
IR0_TOOLCHAIN_DIR := $(call ir0_local_path,toolchain)

endif
