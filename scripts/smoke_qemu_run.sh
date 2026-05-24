#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Run QEMU for a userspace smoke, poll serial log, exit fast on pass/fail tags.
# Harness inits call pause() after success; without early kill smokes wait full timeout.

set -euo pipefail

log_file=""
timeout_sec=300
done_tags=()
fail_regex='_FAIL_REASON|\[FASE[0-9A-Z]+\]\[FAIL\]|^BUSYBOX_FAIL|^EXEC_ONLY_FAIL'

usage()
{
	echo "usage: $0 --log FILE [--timeout SEC] [--done TAG]... [--fail-regex RE] -- QEMU_ARGS..." >&2
	exit 2
}

while [[ $# -gt 0 ]]; do
	case "$1" in
	--log)
		log_file="${2:-}"
		shift 2
		;;
	--timeout)
		timeout_sec="${2:-}"
		shift 2
		;;
	--done)
		done_tags+=("${2:-}")
		shift 2
		;;
	--fail-regex)
		fail_regex="${2:-}"
		shift 2
		;;
	--)
		shift
		break
		;;
	-h|--help)
		usage
		;;
	*)
		echo "$0: unknown option: $1" >&2
		usage
		;;
	esac
done

if [[ -z "$log_file" ]] || [[ $# -eq 0 ]]; then
	usage
fi

if [[ ${#done_tags[@]} -eq 0 ]]; then
	echo "$0: at least one --done TAG is required" >&2
	exit 2
fi

rm -f "$log_file"
stdbuf -oL -eL "$@" >"$log_file" 2>&1 &
qpid=$!

reason="timeout"
start=$SECONDS

while (( SECONDS - start < timeout_sec )); do
	if ! kill -0 "$qpid" 2>/dev/null; then
		reason="qemu_exit"
		break
	fi

	if [[ -s "$log_file" ]] && grep -qE "$fail_regex" "$log_file"; then
		reason="fail_tag"
		break
	fi

	all_done=1
	for tag in "${done_tags[@]}"; do
		if ! grep -qF "$tag" "$log_file" 2>/dev/null; then
			all_done=0
			break
		fi
	done
	if [[ "$all_done" -eq 1 ]]; then
		reason="done_tag"
		break
	fi

	sleep 1
done

if kill -0 "$qpid" 2>/dev/null; then
	kill "$qpid" 2>/dev/null || true
fi
wait "$qpid" 2>/dev/null || true

echo "SMOKE_QEMU_AUTOKILL reason=$reason elapsed=$((SECONDS - start))s log=$log_file"
exit 0
