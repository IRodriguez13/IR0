#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Pre-render a subset of IR0 mandoc pages to ASCII for the guest BusyBox `man`
# applet (cat7/). Guest has no nroff/mandoc — only less/more as pager.
#
# Output tree (ready to inject under /):
#   build/guest-man/usr/share/man/cat7/IR0-<slug>.7
#
# Env:
#   IR0_GUEST_MANDOC_PAGES  override page map (space-separated SRC [GUEST] pairs)
#   IR0_GUEST_MANDOC_OUT    output root (default: build/guest-man)
#
# Note: guest basenames must be ≤14 chars (MINIX v1). Long titles map to
# IR0-uspace / IR0-onboard on disk.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

OUT_ROOT="${IR0_GUEST_MANDOC_OUT:-${ROOT}/build/guest-man}"
CAT7="${OUT_ROOT}/usr/share/man/cat7"
MDOC_DIR="${ROOT}/build/mandoc/en"

# Source man_name (build/mandoc/en/<src>.7) → guest basename (≤14 chars: MINIX v1).
# Format each line: SRC_MAN_NAME [GUEST_BASENAME]
# Guest invoke: man <GUEST_BASENAME>  (BusyBox looks for cat7/<name>.7)
DEFAULT_PAGE_MAP=(
	"IR0-boot"
	"IR0-userspace IR0-uspace"
	"IR0-onboarding IR0-onboard"
	"IR0-vfs"
	"IR0-syscalls"
	"IR0-tty"
	"IR0-process"
)

# MINIX v1 directory entry name field (inject_init_minix.NAME_LEN).
MINIX_NAME_MAX=14

if [ -n "${IR0_GUEST_MANDOC_PAGES:-}" ]; then
	# Override: space-separated "SRC" or "SRC GUEST" tokens (same as map lines).
	# shellcheck disable=SC2206
	DEFAULT_PAGE_MAP=(${IR0_GUEST_MANDOC_PAGES})
fi

if ! command -v mandoc >/dev/null 2>&1; then
	echo "  WARN    prepare_guest_mandocs: host 'mandoc' not found — skipping" >&2
	echo "          Install mandoc (e.g. apt install mandoc) for guest man pages." >&2
	echo "          Boot does not require them; set IR0_GUEST_MANDOCS=0 to silence." >&2
	exit 0
fi

echo "  MANDOC  Building English mdoc pages (mandoc-only, no host install)..."
python3 "${ROOT}/scripts/build_mandocs.py" \
	--lang en \
	--mandoc-only \
	--no-install \
	--yes \
	--no-lint

mkdir -p "${CAT7}"
rm -f "${CAT7}"/IR0-*.7

count=0
for entry in "${DEFAULT_PAGE_MAP[@]}"; do
	# shellcheck disable=SC2086
	set -- ${entry}
	src_name="$1"
	guest_name="${2:-$1}"
	src="${MDOC_DIR}/${src_name}.7"
	guest_file="${guest_name}.7"
	if [ "${#guest_file}" -gt "${MINIX_NAME_MAX}" ]; then
		echo "✗ prepare_guest_mandocs: '${guest_file}' is ${#guest_file} chars (MINIX max ${MINIX_NAME_MAX})" >&2
		echo "  Shorten the guest basename in the page map." >&2
		exit 1
	fi
	if [ ! -f "${src}" ]; then
		echo "✗ prepare_guest_mandocs: missing ${src}" >&2
		echo "  (run: make mandocs-en — or check man_name in build_mandocs.py)" >&2
		exit 1
	fi
	dst="${CAT7}/${guest_file}"
	# Pre-rendered ASCII: BusyBox man uses pager only for catN/ (no nroff).
	mandoc -Tascii -I os=IR0 "${src}" > "${dst}"
	if grep -qE '^\.(Sh|Pp|Nm|Nd)[[:space:]]' "${dst}" 2>/dev/null; then
		echo "✗ prepare_guest_mandocs: ${dst} still looks like raw mdoc" >&2
		exit 1
	fi
	if [ ! -s "${dst}" ]; then
		echo "✗ prepare_guest_mandocs: empty render ${dst}" >&2
		exit 1
	fi
	if [ "${src_name}" != "${guest_name}" ]; then
		echo "  CAT7    ${dst#${ROOT}/}  (from ${src_name})"
	else
		echo "  CAT7    ${dst#${ROOT}/}"
	fi
	count=$((count + 1))
done

date -u +%Y-%m-%dT%H:%M:%SZ > "${OUT_ROOT}/.stamp"
echo "✓ prepare-guest-mandocs → ${CAT7} (${count} pages)"
echo "  IR0_GUEST_MANDOC_DIR=${OUT_ROOT}"
