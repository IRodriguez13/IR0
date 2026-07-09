#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Agent-oriented diagnostics: fast gates, ABI contract audit, log decode.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

usage() {
	cat <<'EOF'
ir0-diag — fast agent diagnostics for IR0

Usage:
  scripts/ir0-diag.sh fast              test-fast + kernel-tests (~20s)
  scripts/ir0-diag.sh contract NAME     single linux-abi-audit (fresh ISO)
  scripts/ir0-diag.sh log FILE          ktm-report + flight decode + ABI hints
  scripts/ir0-diag.sh smoke-userspace   runit boot smoke (userspace quick)

Env:
  LINUX_ABI_SKIP_KTEST=1   skip kernel-tests inside audit (faster, less strict)
EOF
}

mode="${1:-}"
arg="${2:-}"

case "$mode" in
fast)
	echo "== ir0-diag fast =="
	make -s test-fast
	make -s kernel-tests
	echo "IR0_DIAG_FAST_OK"
	;;
contract)
	[[ -n "$arg" ]] || { echo "missing contract name" >&2; usage; exit 2; }
	echo "== ir0-diag contract $arg =="
	rm -f kernel-x64-userspace.iso
	make -s kernel-x64-userspace.iso
	python3 scripts/linux_abi_audit.py --contract "$arg"
	;;
log)
	[[ -n "$arg" && -f "$arg" ]] || { echo "missing log file: $arg" >&2; exit 2; }
	echo "== ir0-diag log $arg =="
	make -s ktm-report LOG="$arg" || true
	python3 scripts/ktm_flight_decode.py "$arg" --tail 32 || true
	if grep -q '\[LINUX_ABI_AUDIT\]' "$arg"; then
		contract="$(grep -oE '\[LINUX_ABI_AUDIT\]\[[a-z0-9_]+\]' "$arg" | head -1 | sed 's/.*\[\([^]]*\)\]$/\1/')"
		if [[ -n "$contract" ]]; then
			parse_script="scripts/linux_abi/parse_${contract}_trace.py"
			if [[ -f "$ROOT/$parse_script" ]]; then
				python3 "$ROOT/$parse_script" ir0 "$arg" /tmp/ir0-diag-trace.json
			else
				python3 "$ROOT/scripts/linux_abi/parse_simple_trace.py" \
					"$contract" ir0 "$arg" /tmp/ir0-diag-trace.json
			fi
			echo "  parsed -> /tmp/ir0-diag-trace.json"
		fi
	fi
	echo "IR0_DIAG_LOG_OK"
	;;
smoke-userspace)
	echo "== ir0-diag smoke-userspace =="
	IR0_INCLUDE_QA=1 make -s smoke-runit-boot
	echo "IR0_DIAG_SMOKE_USERSPACE_OK"
	;;
help|-h|--help|"")
	usage
	exit 0
	;;
*)
	echo "unknown mode: $mode" >&2
	usage
	exit 2
	;;
esac
