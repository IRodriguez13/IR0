#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Build a static musl GNU make for in-guest IR0 toolchain experiments.
#
# Output: setup/pid1/devtools_staging/bin/make

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STAGE="${ROOT}/setup/pid1/devtools_staging"
SRC_DIR="${GMAKE_SRC:-/tmp/gmake-ir0}"
VER="${GMAKE_VER:-4.4.1}"
TARBALL="make-${VER}.tar.gz"
URL="https://ftp.gnu.org/gnu/make/${TARBALL}"
MUSL_CC="${MUSL_CC:-$(command -v x86_64-linux-musl-gcc 2>/dev/null || command -v musl-gcc)}"

if [ -z "${MUSL_CC}" ]; then
	echo "✗ musl cross compiler not found (set MUSL_CC=...)" >&2
	exit 1
fi

mkdir -p "${STAGE}/bin" "${STAGE}/usr/share/ir0-devtools"

if [ ! -d "${SRC_DIR}/make-${VER}" ]; then
	echo "  GMAKE   Fetching ${TARBALL}"
	mkdir -p "${SRC_DIR}"
	if [ ! -f "${SRC_DIR}/${TARBALL}" ]; then
		curl -fsSL -o "${SRC_DIR}/${TARBALL}" "${URL}" \
			|| wget -q -O "${SRC_DIR}/${TARBALL}" "${URL}"
	fi
	tar -C "${SRC_DIR}" -xzf "${SRC_DIR}/${TARBALL}"
fi

echo "  GMAKE   Building static make-${VER} with ${MUSL_CC}"
(
	cd "${SRC_DIR}/make-${VER}"
	# Avoid rebuilding configure when sources are already configured for another CC.
	if [ ! -f Makefile ] || ! grep -q "${MUSL_CC}" config.log 2>/dev/null; then
		make distclean >/dev/null 2>&1 || true
		CC="${MUSL_CC}" CFLAGS="-static -Os" LDFLAGS="-static" \
			./configure --host=x86_64-linux-musl --disable-nls --without-guile
	fi
	make -j"$(nproc)"
	# Prefer the real binary (not libtool wrapper).
	if [ -x make ]; then
		cp -f make "${STAGE}/bin/make"
	elif [ -x make/make ]; then
		cp -f make/make "${STAGE}/bin/make"
	else
		echo "✗ make binary missing after build" >&2
		exit 1
	fi
	chmod 755 "${STAGE}/bin/make"
)

# Tiny guest smoke tree: tcc + make can build hello without host tools.
cat >"${STAGE}/usr/share/ir0-devtools/hello.c" <<'EOF'
#include <stdio.h>
int main(void)
{
	puts("hello IR0 toolchain");
	return 0;
}
EOF
cat >"${STAGE}/usr/share/ir0-devtools/Makefile" <<'EOF'
# In-guest IR0 toolchain sample (TinyCC + GNU make).
CC ?= tcc
# TinyCC needs an explicit library path on IR0 rootfs.
CFLAGS ?= -B/lib/tcc -static -Os
hello: hello.c
	$(CC) $(CFLAGS) -o hello hello.c
clean:
	rm -f hello
EOF

# Host sanity: binary is ELF static.
file "${STAGE}/bin/make" | grep -qi elf || {
	echo "✗ staged make is not ELF" >&2
	exit 1
}
echo "✓ build-gmake-static OK → ${STAGE}/bin/make"
