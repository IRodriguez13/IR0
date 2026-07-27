#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# ensure-host-deps.sh — Run deptest for a profile; if required tools are
# missing, explain what is needed and optionally install with user consent.
#
# Usage (from IR0 tree):
#   ./scripts/ensure-host-deps.sh
#   PROFILE=userspace ./scripts/ensure-host-deps.sh
#
# Env:
#   PROFILE              deptest profile (default: userspace for first-boot)
#   IR0_DEPS_INSTALL     ask (default) | yes | never | 0
#                        ask  — prompt [y/N] when stdin is a TTY
#                        yes  — run package manager without prompt (CI / pre-auth)
#                        never|0 — report only, exit 1 if missing
#
# Never installs silently. sudo asks for the password when needed.

set -euo pipefail

# Prefer absolute coreutils so a narrowed PATH still locates this script.
_dirname() { command -v dirname >/dev/null 2>&1 && dirname "$@" || /usr/bin/dirname "$@"; }
_mktemp() { command -v mktemp >/dev/null 2>&1 && mktemp "$@" || /usr/bin/mktemp "$@"; }

ROOT="$(CDPATH= cd -- "$(_dirname "$0")/.." && pwd)"
cd "${ROOT}"

PROFILE="${PROFILE:-userspace}"
MODE="${IR0_DEPS_INSTALL:-ask}"
case "$MODE" in
ask|yes|never|0|no) ;;
*)
	echo "[ensure-host-deps] Unknown IR0_DEPS_INSTALL=${MODE} (use ask|yes|never)" >&2
	exit 2
	;;
esac

detect_pm() {
	if command -v apt-get >/dev/null 2>&1 || [ -x /usr/bin/apt-get ]; then
		echo apt
	elif command -v pacman >/dev/null 2>&1 || [ -x /usr/bin/pacman ]; then
		echo pacman
	elif command -v dnf >/dev/null 2>&1 || [ -x /usr/bin/dnf ]; then
		echo dnf
	else
		echo unknown
	fi
}

run_deptest() {
	local list="$1"
	: >"$list"
	set +e
	# Restore a sane PATH for deptest probes (tools live under /usr/bin).
	PATH="${PATH:-/usr/bin:/bin}:/usr/bin:/bin:/usr/local/bin"
	export PATH
	IR0_DEPS_LIST_FILE="$list" PROFILE="$PROFILE" \
		"${ROOT}/scripts/deptest.sh"
	local rc=$?
	set -e
	return "$rc"
}

build_install_cmd() {
	local list="$1"
	local pm="$2"
	local pkgs=""
	local line apt_pkgs pac_pkgs dnf_pkgs

	apt_pkgs=""
	pac_pkgs=""
	dnf_pkgs=""
	while IFS=$'\t' read -r apt_pkgs_line pac_pkgs_line dnf_pkgs_line || [ -n "${apt_pkgs_line:-}" ]; do
		[ -z "${apt_pkgs_line:-}" ] && continue
		apt_pkgs="${apt_pkgs} ${apt_pkgs_line}"
		pac_pkgs="${pac_pkgs} ${pac_pkgs_line}"
		dnf_pkgs="${dnf_pkgs} ${dnf_pkgs_line}"
	done <"$list"

	# Unique words (stable order)
	uniq_words() {
		# shellcheck disable=SC2086
		printf '%s\n' $1 | awk 'NF && !seen[$0]++'
	}

	case "$pm" in
	apt)
		pkgs=$(uniq_words "$apt_pkgs" | tr '\n' ' ' | sed 's/[[:space:]]*$//')
		[ -n "$pkgs" ] || return 1
		echo "sudo apt-get install -y ${pkgs}"
		;;
	pacman)
		pkgs=$(uniq_words "$pac_pkgs" | tr '\n' ' ' | sed 's/[[:space:]]*$//')
		[ -n "$pkgs" ] || return 1
		echo "sudo pacman -S --needed --noconfirm ${pkgs}"
		;;
	dnf)
		pkgs=$(uniq_words "$dnf_pkgs" | tr '\n' ' ' | sed 's/[[:space:]]*$//')
		[ -n "$pkgs" ] || return 1
		echo "sudo dnf install -y ${pkgs}"
		;;
	*)
		return 1
		;;
	esac
}

LIST="$(_mktemp)"
trap 'rm -f "$LIST"' EXIT

echo "=== IR0 ensure-host-deps (PROFILE=${PROFILE}) ==="

if [ "${IR0_DEPS_SELFTEST:-}" = "1" ]; then
	# Exercise the consent / install-command path without removing host tools.
	printf 'nasm\tnasm\tnasm\nmusl-tools\tmusl musl-gcc\tmusl-gcc\n' >"$LIST"
	echo "[ensure-host-deps] SELFTEST: simulating missing nasm + musl-tools"
	fake_fail=1
else
	fake_fail=0
	if run_deptest "$LIST"; then
		echo "[ensure-host-deps] OK — required host tools present"
		exit 0
	fi
fi

PM="$(detect_pm)"
if [ "$fake_fail" -eq 0 ]; then
	echo ""
	echo "[ensure-host-deps] Required host dependencies are missing."
fi

if [ ! -s "$LIST" ]; then
	echo "  deptest failed without a package mapping (version/usability issue)."
	echo "  Fix the messages above, or see SETUP.md"
	echo "  Then: make deptest PROFILE=${PROFILE}"
	exit 1
fi

INSTALL_CMD=""
if [ "$PM" != "unknown" ]; then
	INSTALL_CMD="$(build_install_cmd "$LIST" "$PM" || true)"
fi

if [ -z "$INSTALL_CMD" ]; then
	echo "  Could not build an install command for package manager '${PM}'."
	echo "  Install the packages listed above manually (see SETUP.md),"
	echo "  then re-run: make first-boot   # or: make deptest PROFILE=${PROFILE}"
	exit 1
fi

echo "  Proposed install:"
echo "    ${INSTALL_CMD}"
echo ""

do_install=0
case "$MODE" in
yes)
	do_install=1
	;;
never|0|no)
	do_install=0
	;;
ask)
	ans=""
	# Prefer /dev/tty so prompts work even when stdout is logged; fall back to stdin.
	if { printf "Install now? [y/N] " >/dev/tty; } 2>/dev/null \
		&& { read -r ans </dev/tty; } 2>/dev/null; then
		:
	else
		printf "Install now? [y/N] "
		if ! read -r ans; then
			echo "[ensure-host-deps] Non-interactive stdin — not installing."
			echo "  Re-run with a TTY, or: IR0_DEPS_INSTALL=yes make first-boot"
			echo "  Or install manually per SETUP.md and: make deptest PROFILE=${PROFILE}"
			exit 1
		fi
	fi
	case "$ans" in
	y|Y|yes|YES)
		do_install=1
		;;
	*)
		do_install=0
		;;
	esac
	;;
esac

if [ "$do_install" -eq 0 ]; then
	echo "[ensure-host-deps] Declined. Read SETUP.md and install host tools,"
	echo "  then re-run make first-boot (or make deptest PROFILE=${PROFILE})."
	exit 1
fi

if [ "$fake_fail" -eq 1 ]; then
	echo "[ensure-host-deps] SELFTEST: would run: ${INSTALL_CMD}"
	echo "[ensure-host-deps] SELFTEST OK (did not invoke package manager)"
	exit 0
fi

echo "[ensure-host-deps] Running: ${INSTALL_CMD}"
# shellcheck disable=SC2086
eval ${INSTALL_CMD}

echo ""
echo "[ensure-host-deps] Re-checking…"
if run_deptest "$LIST"; then
	echo "[ensure-host-deps] OK — dependencies satisfied"
	exit 0
fi

echo "[ensure-host-deps] Still failing after install. See SETUP.md / make deptest."
exit 1
