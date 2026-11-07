#!/bin/sh
# Oleada A — T1 credibility gates (kernel + ABI slice).
# SPDX-License-Identifier: GPL-3.0-only
set -e
ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export LC_ALL=C
export TZ=UTC

make -s kernel-x64.bin
python3 scripts/architecture_guard.py
make -s -C tests/host run
make -s disk.img
if [ ! -f "$ROOT/kernel-x64-userspace.iso" ]; then
	make -s kernel-x64-userspace.iso
fi
export KERNEL_USERSPACE_ISO="${KERNEL_USERSPACE_ISO:-$ROOT/kernel-x64-userspace.iso}"

for c in pipe wait4 execve process_lifecycle pty_multiplex; do
	echo "  ABI  contract=$c"
	python3 scripts/linux_abi_audit.py --contract "$c"
done

if [ -d "${IR0_ISD_ROOT:-$ROOT/../ISD}" ]; then
	ISD="$(CDPATH= cd -- "${IR0_ISD_ROOT:-$ROOT/../ISD}" && pwd)"
	if [ -f "$ISD/Makefile" ]; then
		echo "  ISD  toolchain-check (gcc pilot slice)"
		(cd "$ISD" && make -s toolchain-check ARCH=x86_64) || true
	fi
fi

echo "✓ wave-a-gates OK"
