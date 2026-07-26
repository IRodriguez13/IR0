#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# First-time wire-up: clone IR0-userspace (if missing), export UAPI, build
# the minimal product rootfs (runit + BusyBox), inject into disk.img.
#
# Usage (from IR0 tree):
#   ./scripts/bootstrap-userspace.sh
#   make first-boot          # same + ISO
#   make run                 # QEMU GTK (BusyBox ash via getty)
#
# Env:
#   IR0_USERSPACE_ROOT   sibling path (default: ../IR0-userspace)
#   IR0_USERSPACE_URL    clone URL
#   IR0_PRODUCT_PROFILE  development|production (default: development)
#   IR0_NO_AUTOLOGIN     0|1 (default: 0 for development)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

USERSPACE_ROOT="${IR0_USERSPACE_ROOT:-${ROOT}/../IR0-userspace}"
USERSPACE_URL="${IR0_USERSPACE_URL:-https://github.com/IRodriguez13/IR0-userspace.git}"
PROFILE="${IR0_PRODUCT_PROFILE:-development}"
NO_AUTOLOGIN="${IR0_NO_AUTOLOGIN:-0}"

echo "=== IR0 ↔ IR0-userspace bootstrap ==="
echo "  kernel:    ${ROOT}"
echo "  userspace: ${USERSPACE_ROOT}"
echo "  profile:   ${PROFILE}"

if [ ! -f "${USERSPACE_ROOT}/Makefile" ]; then
	parent="$(dirname "${USERSPACE_ROOT}")"
	mkdir -p "${parent}"
	echo "  CLONE    ${USERSPACE_URL} → ${USERSPACE_ROOT}"
	git clone --depth 1 "${USERSPACE_URL}" "${USERSPACE_ROOT}"
else
	echo "  OK       sibling already present"
fi

export IR0_USERSPACE_ROOT="${USERSPACE_ROOT}"

echo "  HEADERS  make headers_install → sibling sysroot"
make -s headers_install DESTDIR="${USERSPACE_ROOT}/out/sysroot"

echo "  CHECK    make check-userspace"
make -s check-userspace

echo "  ROOTFS   runit + BusyBox → disk.img (profile=${PROFILE})"
IR0_PRODUCT_PROFILE="${PROFILE}" IR0_NO_AUTOLOGIN="${NO_AUTOLOGIN}" \
	make -s load-userspace-runit

echo "  ISO      kernel-x64-userspace.iso"
make -s kernel-x64-userspace.iso

cat <<EOF

✓ Distro mínima lista (runit PID1 + BusyBox ash).

Arrancar:
  make run              # GTK + serial (recomendado)
  make run-console      # solo serial (sin ventana)

Dentro del guest (tras login / autologin development):
  busybox
  ls /
  cat /proc/version
  echo hello
  man IR0-boot
  man IR0-uspace
  man -w IR0-tty

Toolchain in-guest (TinyCC + GNU make) es opcional:
  make load-userspace-devtools   # o: IR0_WITH_DEVTOOLS=1 make run

Docs: Documentation/USERSPACE.md
EOF
