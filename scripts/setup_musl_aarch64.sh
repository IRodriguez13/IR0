#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Install aarch64-linux-musl cross toolchain under toolchain/ (repo-local).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/toolchain/aarch64-linux-musl"
GCC="$DEST/bin/aarch64-linux-musl-gcc"
TGZ_URL="${MUSL_AARCH64_TGZ_URL:-https://musl.cc/aarch64-linux-musl-cross.tgz}"
CACHE_TGZ="${MUSL_AARCH64_TGZ:-/tmp/aarch64-linux-musl-cross.tgz}"

if [ -x "$GCC" ]; then
	echo "✓ musl aarch64 already present: $GCC"
	"$GCC" -dumpmachine
	exit 0
fi

mkdir -p "$ROOT/toolchain"
TMPDIR="$(mktemp -d /tmp/ir0-musl-aarch64.XXXXXX)"
cleanup() { rm -rf "$TMPDIR"; }
trap cleanup EXIT

if [ ! -f "$CACHE_TGZ" ]; then
	echo "  DL      $TGZ_URL"
	curl -L --fail --retry 3 -o "$CACHE_TGZ" "$TGZ_URL"
fi

echo "  EXTRACT $CACHE_TGZ → $DEST"
tar -xzf "$CACHE_TGZ" -C "$TMPDIR"
# musl.cc layout: aarch64-linux-musl-cross/{bin,lib,...}
SRC="$(find "$TMPDIR" -maxdepth 2 -type d -name 'bin' | head -1)"
SRC="$(dirname "$SRC")"
rm -rf "$DEST"
mv "$SRC" "$DEST"

if [ ! -x "$GCC" ]; then
	echo "✗ expected $GCC after extract" >&2
	exit 1
fi

echo "✓ installed musl aarch64: $GCC"
"$GCC" -dumpmachine
