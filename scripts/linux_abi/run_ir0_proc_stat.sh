#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Run proc_stat_probe under IR0 via injected init.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/build/linux_abi_audit/ir0/proc_stat}"

exec bash "$ROOT/scripts/linux_abi/run_ir0_workload.sh" \
	proc_stat proc_stat_probe PROC_STATOK "$OUT"
