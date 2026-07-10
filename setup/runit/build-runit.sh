#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Build static runit tools for IR0 userspace (musl cross or native musl-gcc).
#
# Source: https://smarden.org/runit/ (runit-2.3.1.tar.gz)
# Void template reference: void-packages srcpkgs/runit/template

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="${RUNIT_VERSION:-2.3.1}"
THIRD_PARTY="${ROOT}/setup/third-party"
SRC_DIR="${RUNIT_SRC:-${THIRD_PARTY}/runit-${VERSION}}"
TARBALL="${THIRD_PARTY}/runit-${VERSION}.tar.gz"
URL="https://smarden.org/runit/runit-${VERSION}.tar.gz"
OUT_DIR="${ROOT}/setup/runit/bin"
STAGE_BIN="${ROOT}/setup/runit/stage-bin"
CC="${MUSL_CC:-$(command -v x86_64-linux-musl-gcc 2>/dev/null || command -v musl-gcc 2>/dev/null || true)}"

if [ -z "$CC" ]; then
	echo "✗ musl cross compiler not found (set MUSL_CC=...)" >&2
	exit 1
fi

mkdir -p "$THIRD_PARTY" "$OUT_DIR"

if [ ! -d "$SRC_DIR/src" ]; then
	echo "  RUNIT   Fetching runit-${VERSION}..."
	curl -fsSL "$URL" -o "$TARBALL"
	rm -rf "$SRC_DIR"
	mkdir -p "$THIRD_PARTY"
	tar -xzf "$TARBALL" -C "$THIRD_PARTY"
	if [ -d "${THIRD_PARTY}/admin/runit-${VERSION}" ]; then
		mv "${THIRD_PARTY}/admin/runit-${VERSION}" "$SRC_DIR"
		rmdir "${THIRD_PARTY}/admin" 2>/dev/null || true
	else
		mv "${THIRD_PARTY}/runit-${VERSION}" "$SRC_DIR" 2>/dev/null || true
	fi
fi

echo "  RUNIT   Building with $CC (static)..."
cd "$SRC_DIR/src"

echo "$CC -D_GNU_SOURCE -static -Os -fno-pie" >conf-cc
echo "$CC -static -no-pie" >conf-ld

make -s clean 2>/dev/null || true
make -s sysdeps
make -s runit runit-init runsvdir runsv sv chpst

for bin in runit runit-init runsvdir runsv sv chpst; do
	install -m 0755 "$bin" "$OUT_DIR/$bin"
	file "$OUT_DIR/$bin" | grep -q ELF
done

echo "✓ build-runit OK ($(ls -1 "$OUT_DIR" | tr '\n' ' '))"

echo "  RUNIT   Building IR0 stage/service ELF stubs..."
mkdir -p "$STAGE_BIN"
"$CC" -static -Os -o "$STAGE_BIN/runit_stage1" "${ROOT}/setup/runit/runit_stage1.c"
"$CC" -static -Os -o "$STAGE_BIN/runit_stage2" "${ROOT}/setup/runit/runit_stage2.c"
"$CC" -static -Os -o "$STAGE_BIN/runit_console_run" "${ROOT}/setup/runit/runit_console_run.c"
"$CC" -static -Os -o "$STAGE_BIN/runit_logger_run" "${ROOT}/setup/runit/runit_logger_run.c"
"$CC" -static -Os \
	-DRUNIT_EXEC_PATH='"/bin/f52-harness"' \
	-DRUNIT_START_TAG='"RUNSV_FASE52_START\n"' \
	-o "$STAGE_BIN/runit_fase52_run" "${ROOT}/setup/runit/runit_exec_run.c"
"$CC" -static -Os \
	-DRUNIT_EXEC_PATH='"/bin/doom-smoke"' \
	-DRUNIT_START_TAG='"RUNSV_FASE55D_START\n"' \
	-o "$STAGE_BIN/runit_fase55d_run" "${ROOT}/setup/runit/runit_exec_run.c"
"$CC" -static -Os -o "$STAGE_BIN/runit_fase55d_init" "${ROOT}/setup/runit/runit_fase55d_init.c"
"$CC" -static -Os -o "$STAGE_BIN/runit_power_smoke" "${ROOT}/setup/runit/runit_power_smoke.c"
"$CC" -static -Os -o "$STAGE_BIN/runit_power_run" "${ROOT}/setup/runit/runit_power_run.c"
for bin in runit_stage1 runit_stage2 runit_console_run runit_logger_run \
	runit_fase52_run runit_fase55d_run runit_fase55d_init \
	runit_power_smoke runit_power_run; do
	file "$STAGE_BIN/$bin" | grep -q ELF
done
echo "✓ runit stage ELF stubs OK"
