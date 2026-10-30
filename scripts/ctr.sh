#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# CTR gates — kernel + architecture boundary + config matrix + host tests.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

RUN_KTM="${CTR_KTM:-0}"

echo "== CTR: kernel-x64.bin =="
make -s ARCH=x86_64 kernel-x64.bin

echo "== CTR: arch-guard =="
make -s arch-guard

echo "== CTR: build-matrix-min =="
make -s build-matrix-min

echo "== CTR: tests/host =="
make -s -C tests/host run

if [[ "$RUN_KTM" == "1" ]]; then
	echo "== CTR: ktest (KTM in QEMU) =="
	make -s ARCH=x86_64 ktest
fi

echo "CTR_OK"
