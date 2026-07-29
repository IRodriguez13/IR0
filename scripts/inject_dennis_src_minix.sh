#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Stage Dennis Ritchie playground under /heart/dennis on MINIX disk.img:
#   README + short-name samples (MINIX v1 name ≤14 chars).
# Full IR0 tree (long names) is mounted at boot by runit stage1:
#   mount -t 9p dennis /heart/dennis/src
# (QEMU: make run attaches -virtfs mount_tag=dennis)
#
# Usage: inject_dennis_src_minix.sh [disk.img]

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DISK="${1:-${ROOT}/disk.img}"
INJECT="${ROOT}/scripts/inject_init_minix.py"
STAGE="$(mktemp -d "${TMPDIR:-/tmp}/ir0-dennis.XXXXXX")"
trap 'rm -rf "${STAGE}"' EXIT

if [ "${IR0_INSTALL_DENNIS:-1}" = "0" ]; then
	echo "  SKIP    dennis src (IR0_INSTALL_DENNIS=0)"
	exit 0
fi

if [ ! -f "${DISK}" ]; then
	echo "✗ inject_dennis: missing ${DISK}" >&2
	exit 1
fi

mkdir -p "${STAGE}/heart/dennis/src"

cat > "${STAGE}/heart/dennis/README" <<'EOF'
Dennis Ritchie playground — IR0 sources for editors & toolchain.

Full kernel/userspace tree (host IR0 via virtio-9p):
  ls /heart/dennis/src
  ls /heart/dennis/src/kernel
  nano /heart/dennis/src/kernel/main.c

runit stage1 mounts tag "dennis" as root before login
(QEMU: make run → -virtfs mount_tag=dennis). Look for
serial tag DENNIS_9P_MOUNT_OK. If you only see hello.c,
the 9p device is missing (IR0_DENNIS_9P=0).

Offline samples on the MINIX disk (short names, ≤14 chars):
  /heart/dennis/src/hello.c
  /heart/dennis/src/Makefile

Build sample with TinyCC:
  tcc -B/lib/tcc -static -o /tmp/hi /heart/dennis/src/hello.c

Porting tips: man IR0-port
EOF

cat > "${STAGE}/heart/dennis/src/hello.c" <<'EOF'
/* Minimal C sample for nano/vi + tcc on IR0. */
#include <stdio.h>

int main(void)
{
	puts("hello from /heart/dennis");
	return 0;
}
EOF

cat > "${STAGE}/heart/dennis/src/Makefile" <<'EOF'
# Guest sample — TinyCC static link
CC ?= tcc
CFLAGS ?= -B/lib/tcc -static -O2

all: hello

hello: hello.c
	$(CC) $(CFLAGS) -o hello hello.c

clean:
	rm -f hello
EOF

cat > "${STAGE}/heart/dennis/src/NOTES.txt" <<'EOF'
Short-name files only fit on MINIX v1 (14-char names).
Full IR0 tree appears here after stage1 9p mount (dennis).
See /heart/dennis/README and: man IR0-port
EOF

echo "  DENNIS  /heart/dennis (README + src samples)"
python3 "${INJECT}" --mode 0644 "${DISK}" "${STAGE}/heart/dennis/README" heart/dennis/README
python3 "${INJECT}" --mode 0644 "${DISK}" "${STAGE}/heart/dennis/src/hello.c" heart/dennis/src/hello.c
python3 "${INJECT}" --mode 0644 "${DISK}" "${STAGE}/heart/dennis/src/Makefile" heart/dennis/src/Makefile
python3 "${INJECT}" --mode 0644 "${DISK}" "${STAGE}/heart/dennis/src/NOTES.txt" heart/dennis/src/NOTES.txt

echo "✓ inject_dennis_src_minix OK"
