#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Import a reviewed Linux UAPI snapshot into IR0's public ABI tree.
#
# This is a maintainer action, not part of an end-user build.  It imports the
# *installed* Linux UAPI, never the internal source headers: Linux sanitizes
# several declarations while running `make headers_install`.  The resulting
# headers are versioned in IR0 so ISD never consumes the build host's headers.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
LINUX_ROOT="${1:-${IR0_LINUX_UAPI_SOURCE:-}}"

[ -n "$LINUX_ROOT" ] || {
	echo "usage: $0 /path/to/linux-source" >&2
	exit 2
}
[ -d "$LINUX_ROOT/include/uapi/linux" ] || {
	echo "✗ not a Linux UAPI source: $LINUX_ROOT" >&2
	exit 1
}

copy_tree()
{
	local source="$1" destination="$2"
	mkdir -p "$destination"
	cp -a "$source"/. "$destination"/
}

stage="$(mktemp -d "${TMPDIR:-/tmp}/ir0-linux-uapi.XXXXXX")"
trap 'rm -rf "$stage"' EXIT

make -sC "$LINUX_ROOT" ARCH=x86 INSTALL_HDR_PATH="$stage/x86_64" headers_install
make -sC "$LINUX_ROOT" ARCH=arm64 INSTALL_HDR_PATH="$stage/arm64" headers_install

# The non-asm portion is architecture-neutral for the ISAs IR0 currently
# supports. Preserve every public include directory so third-party sources can
# depend on normal Linux UAPI headers, then keep asm behind an explicit ISA
# selection in our own headers_install target.
for directory in "$stage/x86_64/include"/*; do
	name="${directory##*/}"
	[ "$name" = asm ] && continue
	copy_tree "$directory" "$ROOT/includes/uapi/$name"
done
copy_tree "$stage/x86_64/include/asm" "$ROOT/includes/uapi/arch/x86_64/asm"
copy_tree "$stage/arm64/include/asm" "$ROOT/includes/uapi/arch/arm64/asm"

# Keep the imported snapshot compatible with IR0's whitespace gate. This is a
# mechanical normalization only; headers_install is the semantic source.
find "$ROOT/includes/uapi" -type f -name '*.h' -exec \
	perl -0pi -e 's/[ \t]+$//mg; s/ +\t/\t/g; s/\n+\z/\n/' {} +

commit="$(git -C "$LINUX_ROOT" rev-parse HEAD 2>/dev/null || echo unknown)"
printf '%s\n' "$commit" > "$ROOT/includes/uapi/LINUX_UAPI_SNAPSHOT"
echo "✓ imported Linux UAPI snapshot $commit"
