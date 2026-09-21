#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Incremental rebuild + idempotence gate (same WORK tree; no rm -rf out/).
#
# Exercises stale-stamp failures that fresh-clone-only CI misses:
#   - idempotence: same PROFILE rebuild twice → stable disk manifest
#   - transition: minimal → desktop → minimal without wiping out/
#
# Usage (from release-check-fresh after Tier 1 build):
#   IR0_ROOT=/work/IR0 ISD_ROOT=/work/ISD scripts/release_check_incremental.sh
set -euo pipefail

IR0_ROOT="${1:?IR0 tree}"
ISD_ROOT="${2:?ISD tree}"
ARCH="${ISD_ARCH:-x86_64}"

cd "$IR0_ROOT"
export IR0_ISD_ROOT="$ISD_ROOT"
export IR0_DEPS_INSTALL=never

manifest_line() {
	local profile="$1"
	local key="$2"
	make -s -C "$ISD_ROOT" ARCH="$ARCH" PROFILE="$profile" IR0_ROOT="$IR0_ROOT" \
		print-artifacts | sed -n "s/^${key}=//p" | tail -1
}

disk_sha() {
	local profile="$1"
	local disk
	disk="$(manifest_line "$profile" ROOT_DISK)"
	if [ ! -f "$disk" ]; then
		echo "✗ missing disk for PROFILE=${profile}: ${disk}" >&2
		return 2
	fi
	sha256sum "$disk" | awk '{print $1}'
}

variant_id() {
	local profile="$1"
	manifest_line "$profile" VARIANT_ID
}

echo "== incremental/idempotence (ARCH=${ARCH}) =="

echo "-- idempotence: ensure-isd-disk ×2 PROFILE=minimal --"
make -s ensure-isd-disk PROFILE=minimal ARCH="$ARCH" ISD_ARCH="$ARCH"
hash_a="$(disk_sha minimal)"
variant_a="$(variant_id minimal)"
make -s ensure-isd-disk PROFILE=minimal ARCH="$ARCH" ISD_ARCH="$ARCH"
hash_b="$(disk_sha minimal)"
variant_b="$(variant_id minimal)"
if [ "$hash_a" = "$hash_b" ] && [ "$variant_a" = "$variant_b" ]; then
	echo "  OK  idempotent minimal disk + variant"
else
	echo "✗ idempotence failed: disk ${hash_a} → ${hash_b}, variant ${variant_a} → ${variant_b}" >&2
	exit 1
fi

echo "-- idempotence: usmang verify ×2 --"
python3 scripts/userspace_manager.py --isd-root "$ISD_ROOT" \
	--profile minimal --arch "$ARCH" verify >/dev/null
python3 scripts/userspace_manager.py --isd-root "$ISD_ROOT" \
	--profile minimal --arch "$ARCH" verify >/dev/null
echo "  OK  usmang verify idempotent"

echo "-- transition: minimal → development → minimal (keep out/) --"
min_before="$(disk_sha minimal)"
make -s ensure-isd-disk PROFILE=development ARCH="$ARCH" ISD_ARCH="$ARCH"
dev_variant="$(variant_id development)"
echo "$dev_variant" | grep -q '^development-' || {
	echo "✗ development variant id unexpected: ${dev_variant}" >&2
	exit 1
}
make -s ensure-isd-disk PROFILE=minimal ARCH="$ARCH" ISD_ARCH="$ARCH"
min_after="$(disk_sha minimal)"
if [ "$min_before" = "$min_after" ]; then
	echo "  OK  minimal disk restored after development transition"
else
	echo "✗ transition left stale minimal disk: ${min_before} → ${min_after}" >&2
	exit 1
fi

echo "-- alt init profiles: resolve + usmang summary --"
for alt in minimal-sysvinit minimal-openrc; do
	pkgs="$(PROFILE="$alt" bash "$ISD_ROOT/scripts/resolve-packages.sh")"
	echo "  OK  resolve PROFILE=${alt}: ${pkgs}"
	python3 scripts/userspace_manager.py --isd-root "$ISD_ROOT" \
		--profile "$alt" --arch "$ARCH" summary >/dev/null
	echo "  OK  usmang summary PROFILE=${alt}"
done

echo "✓ incremental/idempotence OK"
