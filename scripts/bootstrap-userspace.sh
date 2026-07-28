#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# First-time wire-up: check host deps (optional install), clone IR0-userspace
# (if missing), export UAPI, build the product rootfs (runit + BusyBox),
# inject into disk.img, build ISO.
#
# Usage (from IR0 tree):
#   make first-boot
#   ./scripts/bootstrap-userspace.sh
#   make run
#
# Env:
#   IR0_USERSPACE_ROOT   sibling path (default: ../IR0-userspace)
#   IR0_USERSPACE_URL    clone URL
#   IR0_PRODUCT_PROFILE  minimal|development|desktop|appliance (default: minimal)
#   IR0_INJECT_VERBOSE=1 per-file MINIX inject / verify chatter
#   IR0_DEPS_INSTALL     ask (default) | yes | never — see ensure-host-deps.sh

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

USERSPACE_ROOT="${IR0_USERSPACE_ROOT:-${ROOT}/../IR0-userspace}"
USERSPACE_URL="${IR0_USERSPACE_URL:-https://github.com/IRodriguez13/IR0-userspace.git}"
PROFILE="${IR0_PRODUCT_PROFILE:-minimal}"
NO_AUTOLOGIN="${IR0_NO_AUTOLOGIN:-0}"

echo "=== IR0 first-boot (profile=${PROFILE}) ==="
echo "  kernel     ${ROOT}"
echo "  userspace  ${USERSPACE_ROOT}"

# Host tools first — before clone/fetch — so a missing musl/grub does not
# leave a half-built sibling tree. Asks y/N (or IR0_DEPS_INSTALL=yes/never).
chmod +x "${ROOT}/scripts/ensure-host-deps.sh" "${ROOT}/scripts/deptest.sh"
PROFILE=userspace "${ROOT}/scripts/ensure-host-deps.sh"

# Product .config — clean clones have none (gitignored). Match SETUP.md.
if [ ! -f "${ROOT}/.config" ]; then
	echo "  CONFIG    make defconfig (no .config yet)"
	make -s defconfig
fi

# Skip Doom/IWAD inject on the default first-boot path (override with =1).
export IR0_INSTALL_KEN_GAMES="${IR0_INSTALL_KEN_GAMES:-0}"

if [ ! -f "${USERSPACE_ROOT}/Makefile" ]; then
	parent="$(dirname "${USERSPACE_ROOT}")"
	mkdir -p "${parent}"
	echo "  CLONE      ${USERSPACE_URL}"
	git clone --depth 1 "${USERSPACE_URL}" "${USERSPACE_ROOT}"
fi

export IR0_USERSPACE_ROOT="${USERSPACE_ROOT}"
export IR0_ROOT="${ROOT}"

echo "  FETCH      upstream sources"
make -s -C "${USERSPACE_ROOT}" fetch

echo "  HEADERS    UAPI -> ${USERSPACE_ROOT}/sysroot"
make -s -C "${USERSPACE_ROOT}" headers IR0_ROOT="${ROOT}"

make -s check-userspace

echo "  BUILD      packages + services"
make -s -C "${USERSPACE_ROOT}" build ARCH=x86_64

echo "  ROOTFS     ${PROFILE} -> disk.img"
IR0_PRODUCT_PROFILE="${PROFILE}" IR0_NO_AUTOLOGIN="${NO_AUTOLOGIN}" \
	make -s load-userspace-runit

echo "  ISO        kernel-x64-userspace.iso"
make -s kernel-x64-userspace.iso

cat <<EOF

Sibling distro ready (runit PID1 + BusyBox, profile=${PROFILE}).
Next:  make run
On first boot: create your username + password (used for login and doas).
Deps:  IR0_DEPS_INSTALL=ask|yes|never (default ask) — see scripts/ensure-host-deps.sh
Doom:  IR0_INSTALL_KEN_GAMES=1 make first-boot   # optional IWAD via REAL_WAD_PATH
Docs:  SETUP.md, Documentation/USERSPACE.md
EOF
