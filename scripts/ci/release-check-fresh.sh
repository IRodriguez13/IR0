#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Fresh-clone release gate (Tier 1 + optional Tier 1.5 boot/guest probes).
#
# Default: git clone IR0 + ISD from GitHub (RELEASE_CHECK_LOCAL=0).
# No host checkout bind mounts, disk.img, .config, or IR0_* shell exports.
#
# Profile tiers (0.0.1):
#   reference  = minimal  (mandatory for fresh-clone-check)
#   supported  = CI may add others; not full usmang cartesian product
#   experimental = ad-hoc; not release gates
#
# Env (explicit only — do not inherit host IR0_*):
#   IR0_REF / ISD_REF     branch to clone (default: dev)
#   PROFILE / ISD_ARCH    userspace profile (default: minimal / x86_64)
#   RELEASE_CHECK_BOOT    1 → Tier 1.5 after Tier 1
#   RELEASE_CHECK_GUEST   1 → guest shell probes after boot
#   RELEASE_CHECK_DOUBLE    1 → run the full pipeline twice in one container
#   RELEASE_CHECK_LOCAL     1 → copy bind-mounted /src trees (WIP dev only)
#   RELEASE_CHECK_DEPS_INSTALL 1 → run ensure-host-deps (IR0_DEPS_INSTALL=yes) before build
set -euo pipefail

git config --global --add safe.directory '*' 2>/dev/null || true

IR0_REF="${IR0_REF:-dev}"
ISD_REF="${ISD_REF:-dev}"
PROFILE="${PROFILE:-minimal}"
ISD_ARCH="${ISD_ARCH:-x86_64}"
RELEASE_CHECK_BOOT="${RELEASE_CHECK_BOOT:-0}"
RELEASE_CHECK_GUEST="${RELEASE_CHECK_GUEST:-0}"
RELEASE_CHECK_DOUBLE="${RELEASE_CHECK_DOUBLE:-0}"
RELEASE_CHECK_LOCAL="${RELEASE_CHECK_LOCAL:-0}"
RELEASE_CHECK_DEPS_INSTALL="${RELEASE_CHECK_DEPS_INSTALL:-0}"

# Drop host leakage: only documented vars below are exported to the build.
unset IR0_ISD_ROOT IR0_DEPS_INSTALL IR0_PRODUCT_PROFILE IR0_MACHINE \
	IR0_USERSPACE_ROOT IR0_USERSPACE_URL IR0_ISD_URL IR0_INCLUDE_QA \
	KERNEL_MAINTAINER_HOME LINUX_TREE 2>/dev/null || true

WORK="${WORK:-/tmp/ir0-release-check.$$}"

if [ "${RELEASE_CHECK_LOCAL}" = "1" ]; then
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

	echo "  copy ${label} from ${src} → ${dest} (working tree; not a release gate)"
	mkdir -p "$dest"
	tar -C "$src" \
		--exclude='./out' \
		--exclude='./kernel-x64-userspace.iso' \
		--exclude='./disk.img' \
		--exclude='./disk.img.runit.stamp' \
		--exclude='./.kernel-manager.lock' \
		--exclude='./.config' \
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

run_once() {
	local pass="$1"
	local work="$2"

	echo ""
	echo "== fresh-clone pass ${pass} WORK=${work} PROFILE=${PROFILE} =="

	if [ "${RELEASE_CHECK_LOCAL}" = "1" ]; then
		copy_repo_tree "$IR0_SRC" "$work/IR0" "IR0"
		copy_repo_tree "$ISD_SRC" "$work/ISD" "ISD"
	else
		clone_repo "$IR0_URL" "$IR0_REF" "$work/IR0"
		clone_repo "$ISD_URL" "$ISD_REF" "$work/ISD"
	fi

	cd "$work/IR0"
	export IR0_ISD_ROOT="$work/ISD"
	export IR0_DEPS_INSTALL=never
	export RELEASE_CHECK_FRESH=1

	chmod +x scripts/deptest.sh scripts/ensure-host-deps.sh
	if [ "$RELEASE_CHECK_DEPS_INSTALL" = "1" ]; then
		echo "== ensure-host-deps (IR0_DEPS_INSTALL=yes) PROFILE=${PROFILE} =="
		PROFILE="$PROFILE" IR0_DEPS_INSTALL=yes scripts/ensure-host-deps.sh
	fi

	make -s release-check-clean PROFILE="$PROFILE"

	if [ "$RELEASE_CHECK_BOOT" = "1" ]; then
		chmod +x scripts/release_check_boot.sh scripts/release_check_guest_probes.py
		PROFILE="$PROFILE" ISD_ARCH="$ISD_ARCH" \
			RELEASE_CHECK_GUEST="$RELEASE_CHECK_GUEST" \
			scripts/release_check_boot.sh
	fi
}

cleanup() {
	rm -rf "$WORK"
}
trap cleanup EXIT

echo "== fresh-clone release-check =="
echo "   LOCAL=${RELEASE_CHECK_LOCAL} IR0_REF=${IR0_REF} ISD_REF=${ISD_REF}"
echo "   PROFILE=${PROFILE} BOOT=${RELEASE_CHECK_BOOT} GUEST=${RELEASE_CHECK_GUEST} DOUBLE=${RELEASE_CHECK_DOUBLE}"

run_once 1 "$WORK"

if [ "$RELEASE_CHECK_DOUBLE" = "1" ]; then
	WORK2="${WORK}.pass2"
	rm -rf "$WORK2"
	run_once 2 "$WORK2"
	rm -rf "$WORK2"
fi

echo "✓ fresh-clone release-check OK"
