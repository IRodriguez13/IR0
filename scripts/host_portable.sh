#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Portable host helpers for shell tooling. Source this file; do not execute it.
#
# Keep GNU-only flags here so a portability regression has one place to fix.
# See Documentation/ai_driven_dev/rules/ir0-ktm-host-portability-safety.md.

# Print the size of an existing regular file in bytes.
host_file_size()
{
	wc -c < "$1" | tr -d '[:space:]'
}

# Print the octal mode of an existing file (GNU stat, then BSD stat).
host_file_mode()
{
	stat -c '%a' "$1" 2>/dev/null || stat -f '%Lp' "$1"
}

# Print a human-readable mtime (GNU stat, then BSD stat).
host_file_mtime()
{
	stat -c '%y' "$1" 2>/dev/null || stat -f '%Sm' "$1"
}

# Copy a disk image, using reflink/sparse when the host cp supports it.
host_copy_disk()
{
	if cp --help 2>/dev/null | grep -q -- '--reflink'; then
		cp --reflink=auto --sparse=always "$1" "$2"
	else
		cp "$1" "$2"
	fi
}
