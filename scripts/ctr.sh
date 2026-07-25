#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# CTR gates — thin wrapper over scripts/ir0-qa.sh (see scripts/make/qa.mk).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

RUN_KTM="${CTR_KTM:-0}"
RUN_SMOKE_TIER1="${CTR_SMOKE_TIER1:-0}"

echo "== CTR: kernel-x64.bin =="
make -s kernel-x64.bin

echo "== CTR: arch-guard =="
"$ROOT/scripts/ir0-qa.sh" arch-guard

echo "== CTR: build-matrix-min =="
"$ROOT/scripts/ir0-qa.sh" build-matrix-min

echo "== CTR: tests/host =="
make -s -C tests/host run

if [[ "$RUN_KTM" == "1" ]]; then
	echo "== CTR: kernel-tests (KTM in QEMU) =="
	"$ROOT/scripts/ir0-qa.sh" kernel-tests
fi

if [[ "$RUN_SMOKE_TIER1" == "1" ]]; then
	echo "== CTR: smoke-tier1 (runit) =="
	"$ROOT/scripts/ir0-qa.sh" smoke-tier1
fi

echo "CTR_OK"
