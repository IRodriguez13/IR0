#!/bin/sh
# Install ~/.cursor/rules/kernel-linux/ into IR0/.cursor/rules/ (symlinks).
# Includes IR0-specific rules + all linux-upstream-*.mdc from home.

set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
exec python3 "$ROOT/scripts/sync_kernel_linux_rules.py" install-project
