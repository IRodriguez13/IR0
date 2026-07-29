#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Deprecated wrapper — delegates to bootstrap-isd.sh (ISD-owned image flow).
echo "note: bootstrap-userspace.sh is deprecated; use scripts/bootstrap-isd.sh / make first-boot"
exec "$(cd "$(dirname "$0")" && pwd)/bootstrap-isd.sh" "$@"
