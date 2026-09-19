#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Inject OpenDoas onto a MINIX disk.img for legacy load-userspace-runit images.
# Minimal ISD profile omits opendoas from the manifest; smokes that need wheel
# elevation (session-chaos, smoke-doas) require /usr/bin/doas + /etc/doas.conf.
#
# Usage: inject_opendoas_minix.sh [disk.img]
# Env:
#   IR0_ISD_ROOT     default: ../ISD
#   IR0_INSTALL_DOAS=0  skip

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DISK="${1:-${ROOT}/disk.img}"
ISD="${IR0_ISD_ROOT:-${ROOT}/../ISD}"
ARCH="${ISD_ARCH:-x86_64}"
INJECT="${ROOT}/scripts/inject_init_minix.py"
DOAS_BIN="${ISD}/out/${ARCH}/product/stage-bin/doas"
DOAS_CONF="${ISD}/rootfs/base/etc/doas.conf"

if [ "${IR0_INSTALL_DOAS:-1}" = "0" ]; then
	echo "  SKIP    opendoas (IR0_INSTALL_DOAS=0)"
	exit 0
fi

if [ ! -f "${DISK}" ]; then
	echo "✗ inject_opendoas: missing ${DISK}" >&2
	exit 1
fi

if [ ! -f "${DOAS_BIN}" ]; then
	echo "✗ inject_opendoas: missing ${DOAS_BIN}" >&2
	echo "  Run: make build-opendoas" >&2
	exit 1
fi

if [ ! -f "${DOAS_CONF}" ]; then
	DOAS_CONF="${ISD}/rootfs/etc/doas.conf"
fi
if [ ! -f "${DOAS_CONF}" ]; then
	echo "✗ inject_opendoas: missing doas.conf under ${ISD}/rootfs" >&2
	exit 1
fi

echo "  DOAS    /usr/bin/doas ← ${DOAS_BIN}"
python3 "${INJECT}" --setuid "${DISK}" "${DOAS_BIN}" usr/bin/doas
python3 "${INJECT}" --mode 0440 "${DISK}" "${DOAS_CONF}" etc/doas.conf
python3 "${ROOT}/scripts/verify_minix_rootfs.py" "${DISK}" /usr/bin/doas /etc/doas.conf
echo "✓ inject_opendoas_minix OK"
