#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Inject in-guest toolchain onto a MINIX disk.img:
#   TinyCC staging (fase52) + GNU make + sample hello/Makefile
#   + /bin/cc → tcc hardlink
#
# Usage: inject_devtools_minix.sh DISK_IMAGE
# Prereq: make build-tcc-fase52 && make build-gmake-static

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DISK="${1:?usage: inject_devtools_minix.sh DISK_IMAGE}"
INJECT="python3 ${ROOT}/scripts/inject_init_minix.py"
TCC_STAGE="${ROOT}/setup/pid1/fase52_staging"
DEV_STAGE="${ROOT}/setup/pid1/devtools_staging"

if [ ! -f "${DISK}" ]; then
	echo "✗ missing disk: ${DISK}" >&2
	exit 1
fi
if [ ! -x "${TCC_STAGE}/bin/tcc" ]; then
	echo "✗ missing ${TCC_STAGE}/bin/tcc — run: make build-tcc-fase52" >&2
	exit 1
fi
for req in \
	"${TCC_STAGE}/lib/tcc/libtcc1.a" \
	"${TCC_STAGE}/lib/tcc/libc.a" \
	"${TCC_STAGE}/lib/tcc/crt1.o" \
	"${TCC_STAGE}/lib/tcc/crti.o" \
	"${TCC_STAGE}/lib/tcc/crtn.o"
do
	if [ ! -f "${req}" ]; then
		echo "✗ missing ${req} — run: make build-tcc-fase52 (FASE52_STAGE=link|full)" >&2
		exit 1
	fi
done
if [ ! -x "${DEV_STAGE}/bin/make" ]; then
	echo "✗ missing ${DEV_STAGE}/bin/make — run: make build-gmake-static" >&2
	exit 1
fi

echo "  DEVTOOLS Injecting TinyCC staging → ${DISK}"
n=0
while IFS= read -r f; do
	rel="${f#${TCC_STAGE}/}"
	${INJECT} "${DISK}" "${f}" "${rel}"
	n=$((n + 1))
done < <(find "${TCC_STAGE}" -type f | sort)
echo "  DEVTOOLS TCC files injected: ${n}"

echo "  DEVTOOLS Injecting GNU make + samples"
${INJECT} "${DISK}" "${DEV_STAGE}/bin/make" bin/make
${INJECT} --hardlink "${DISK}" bin/tcc bin/cc
${INJECT} --hardlink "${DISK}" bin/tcc usr/bin/tcc
${INJECT} --hardlink "${DISK}" bin/tcc usr/bin/cc
${INJECT} --hardlink "${DISK}" bin/make usr/bin/make

if [ -f "${DEV_STAGE}/usr/share/ir0-devtools/hello.c" ]; then
	${INJECT} "${DISK}" "${DEV_STAGE}/usr/share/ir0-devtools/hello.c" \
		usr/share/ir0-devtools/hello.c
	${INJECT} "${DISK}" "${DEV_STAGE}/usr/share/ir0-devtools/Makefile" \
		usr/share/ir0-devtools/Makefile
	# Convenience copies under /root for interactive ash sessions.
	${INJECT} "${DISK}" "${DEV_STAGE}/usr/share/ir0-devtools/hello.c" root/hello.c
	${INJECT} "${DISK}" "${DEV_STAGE}/usr/share/ir0-devtools/Makefile" root/Makefile
fi

python3 "${ROOT}/scripts/verify_minix_rootfs.py" "${DISK}" \
	/bin/tcc /bin/cc /bin/make /usr/bin/make \
	/lib/tcc/libtcc1.a /lib/tcc/libc.a /lib/tcc/crt1.o \
	/usr/share/ir0-devtools/hello.c /root/hello.c \
	/bin/sed /bin/awk /bin/tar

echo "✓ inject-devtools OK (tcc + libtcc1.a + make + busybox filters on ${DISK})"
