#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Fresh-clone Tier-1 (+ optional Tier 1.5 boot) release-check.
set -euo pipefail

# Copied/bind-mounted repos in Docker are owned by the host UID.
git config --global --add safe.directory '*' 2>/dev/null || true

IR0_REF="${IR0_REF:-dev}"
ISD_REF="${ISD_REF:-dev}"
PROFILE="${PROFILE:-minimal}"
WORK="${WORK:-/tmp/ir0-release-check.$$}"
RELEASE_CHECK_BOOT="${RELEASE_CHECK_BOOT:-0}"

# Container/local simulation: mount sibling repos at /src/{IR0,ISD} (see release.mk).
if [ "${RELEASE_CHECK_LOCAL:-0}" = "1" ]; then
	IR0_SRC="${IR0_SRC:-/src/IR0}"
	ISD_SRC="${ISD_SRC:-/src/ISD}"
else
	IR0_URL="${IR0_URL:-https://github.com/IRodriguez13/IR0.git}"
	ISD_URL="${ISD_URL:-https://github.com/IRodriguez13/ISD.git}"
fi

copy_repo_tree() {
	local src="$1"
	local dest="$2"
	local label="$3"

	echo "  copy ${label} from ${src} → ${dest} (working tree)"
	mkdir -p "$dest"
	tar -C "$src" \
		--exclude='./out' \
		--exclude='./kernel-x64-userspace.iso' \
		--exclude='./disk.img' \
		--exclude='./disk.img.runit.stamp' \
		--exclude='./.kernel-manager.lock' \
		-cf - . | tar -C "$dest" -xf -
}

clone_repo() {
	local url="$1"
	local ref="$2"
	local dest="$3"
	local clone_url="$url"

	if [ -d "$url" ]; then
		clone_url="file://$(cd "$url" && pwd)"
	elif [[ "$url" == /* ]]; then
		clone_url="file://$url"
	fi

	echo "  clone ${ref} from ${clone_url} → ${dest}"
	git clone --depth 1 --branch "$ref" "$clone_url" "$dest"
}

cleanup() {
	rm -rf "$WORK"
}
trap cleanup EXIT

mkdir -p "$WORK"
echo "== fresh-clone release-check WORK=$WORK PROFILE=$PROFILE IR0_REF=$IR0_REF ISD_REF=$ISD_REF BOOT=${RELEASE_CHECK_BOOT} =="

if [ "${RELEASE_CHECK_LOCAL:-0}" = "1" ]; then
	copy_repo_tree "$IR0_SRC" "$WORK/IR0" "IR0 dev working tree"
	copy_repo_tree "$ISD_SRC" "$WORK/ISD" "ISD dev working tree"
else
	clone_repo "$IR0_URL" "$IR0_REF" "$WORK/IR0"
	clone_repo "$ISD_URL" "$ISD_REF" "$WORK/ISD"
fi

cd "$WORK/IR0"
export IR0_ISD_ROOT="$WORK/ISD"
export IR0_DEPS_INSTALL=never
export RELEASE_CHECK_FRESH=1

make -s release-check-clean PROFILE="$PROFILE"

if [ "$RELEASE_CHECK_BOOT" = "1" ]; then
	chmod +x scripts/release_check_boot.sh
	PROFILE="$PROFILE" ISD_ARCH="${ISD_ARCH:-x86_64}" scripts/release_check_boot.sh
fi

echo "✓ fresh-clone release-check OK"
