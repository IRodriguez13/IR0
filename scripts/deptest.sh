#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
#
# deptest.sh — Host environment diagnostic for IR0 (alias: make check-env).
#
# Profiles (PROFILE=…; aliases match product image names):
#   desktop | desktop-x86_64  — make ir0 / desktop-x86_64
#   userspace                 — desktop + musl static userspace
#   hub | hub-rpi4            — ARM64 RPi4 UART lab
#   watch | watch-rpi5-stub   — ARM64 rpi5 stub
#   all                       — union of the above
#
# Classification:
#   required              — missing → exit 1
#   optional              — warning only
#   unsupported_version   — present but too old / wrong → exit 1
#   present_but_unusable  — found on PATH but fails a probe → exit 1
#
# Usage:
#   make deptest
#   make check-env PROFILE=desktop-x86_64
#   PROFILE=hub BOARD=rpi4 ./scripts/deptest.sh
#

set -eu

PROFILE_RAW="${PROFILE:-desktop}"
BOARD="${BOARD:-}"
# Prefer absolute helpers so a narrowed PATH still runs diagnostics.
_dirname() { command -v dirname >/dev/null 2>&1 && dirname "$@" || /usr/bin/dirname "$@"; }
_head() { command -v head >/dev/null 2>&1 && head "$@" || /usr/bin/head "$@"; }
_grep() { command -v grep >/dev/null 2>&1 && grep "$@" || /usr/bin/grep "$@"; }
_sed() { command -v sed >/dev/null 2>&1 && sed "$@" || /usr/bin/sed "$@"; }

KERNEL_ROOT="$(CDPATH= cd -- "$(_dirname "$0")/.." && pwd)"
cd "$KERNEL_ROOT"

ERRORS=0
WARNINGS=0
PM=unknown

# Normalize aliases → internal profile + display name for messages
PROFILE="$PROFILE_RAW"
PROFILE_DISPLAY="$PROFILE_RAW"
case "$PROFILE_RAW" in
desktop|generic|"")
	PROFILE=desktop
	PROFILE_DISPLAY=desktop-x86_64
	;;
desktop-x86_64)
	PROFILE=desktop
	PROFILE_DISPLAY=desktop-x86_64
	;;
userspace)
	PROFILE=userspace
	PROFILE_DISPLAY=userspace
	;;
hub|hub-rpi4)
	PROFILE=hub
	PROFILE_DISPLAY=hub-rpi4
	BOARD="${BOARD:-rpi4}"
	;;
watch|watch-rpi5-stub)
	PROFILE=watch
	PROFILE_DISPLAY=watch-rpi5-stub
	BOARD="${BOARD:-rpi5}"
	;;
all)
	PROFILE=all
	PROFILE_DISPLAY=all
	;;
*)
	echo "[deptest] Unknown PROFILE=${PROFILE_RAW}"
	echo "Valid: desktop | desktop-x86_64 | userspace | hub | hub-rpi4 | watch | watch-rpi5-stub | all"
	exit 2
	;;
esac

if [ "${OS:-}" = "Windows_NT" ] || [ -n "${WINDIR:-}" ]; then
	OS_TYPE=windows
else
	OS_TYPE=linux
fi

detect_pm() {
	if command -v apt-get >/dev/null 2>&1; then
		PM=apt
	elif command -v pacman >/dev/null 2>&1; then
		PM=pacman
	elif command -v dnf >/dev/null 2>&1; then
		PM=dnf
	else
		PM=unknown
	fi
}

# Print install hints: args are apt_pkg | pacman_pkg | dnf_pkg
print_install_hints() {
	_apt=$1
	_pac=$2
	_dnf=$3
	echo ""
	case "$PM" in
	apt)
		echo "Ubuntu/Debian:"
		echo "  sudo apt install ${_apt}"
		;;
	pacman)
		echo "Arch:"
		echo "  sudo pacman -S ${_pac}"
		;;
	dnf)
		echo "Fedora:"
		echo "  sudo dnf install ${_dnf}"
		;;
	*)
		echo "Ubuntu/Debian:"
		echo "  sudo apt install ${_apt}"
		echo ""
		echo "Arch:"
		echo "  sudo pacman -S ${_pac}"
		echo ""
		echo "Fedora:"
		echo "  sudo dnf install ${_dnf}"
		;;
	esac
	echo ""
	echo "Re-run:"
	echo "  make deptest PROFILE=${PROFILE_DISPLAY}"
	echo "  # alias: make check-env PROFILE=${PROFILE_DISPLAY}"
}

# $1=tool_name $2=kind (required|optional|unsupported_version|present_but_unusable)
# $3=detail  $4=apt  $5=pacman  $6=dnf
report_issue() {
	_tool=$1
	_kind=$2
	_detail=$3
	_apt=${4:-}
	_pac=${5:-}
	_dnf=${6:-}

	echo ""
	case "$_kind" in
	required)
		echo "[deptest] Missing dependency: ${_tool}"
		echo "Required by profile: ${PROFILE_DISPLAY}"
		ERRORS=$((ERRORS + 1))
		;;
	optional)
		echo "[deptest] Optional missing: ${_tool}"
		echo "Profile: ${PROFILE_DISPLAY} (build continues without this)"
		WARNINGS=$((WARNINGS + 1))
		;;
	unsupported_version)
		echo "[deptest] Unsupported version: ${_tool}"
		echo "Required by profile: ${PROFILE_DISPLAY}"
		echo "${_detail}"
		ERRORS=$((ERRORS + 1))
		;;
	present_but_unusable)
		echo "[deptest] Present but unusable: ${_tool}"
		echo "Required by profile: ${PROFILE_DISPLAY}"
		echo "${_detail}"
		ERRORS=$((ERRORS + 1))
		;;
	*)
		echo "[deptest] ${_kind}: ${_tool}"
		echo "${_detail}"
		ERRORS=$((ERRORS + 1))
		;;
	esac
	if [ -n "$_detail" ] && [ "$_kind" = "required" ]; then
		echo "${_detail}"
	fi
	if [ -n "$_apt" ]; then
		print_install_hints "$_apt" "$_pac" "$_dnf"
	elif [ "$_kind" = "optional" ]; then
		echo ""
	fi
}

ok_line() {
	echo "  OK  $1"
}

# require_cmd LABEL CMD [version args...] — packages via env before call is awkward;
# use require_pkg wrapper instead.
require_cmd() {
	_label=$1
	_cmd=$2
	_apt=$3
	_pac=$4
	_dnf=$5
	shift 5
	if ! command -v "$_cmd" >/dev/null 2>&1; then
		report_issue "$_cmd" required "  (${_label})" "$_apt" "$_pac" "$_dnf"
		return 1
	fi
	_ver=$("$_cmd" "$@" 2>&1 | _head -1 || true)
	ok_line "${_label}: ${_ver}"
	return 0
}

optional_cmd() {
	_label=$1
	_cmd=$2
	if command -v "$_cmd" >/dev/null 2>&1; then
		_ver=$("$_cmd" --version 2>&1 | _head -1 || true)
		ok_line "${_label}: ${_ver}"
		return 0
	fi
	report_issue "$_cmd" optional "(${_label})"
	return 0
}

# Parse major.minor from a version string (first two integers).
parse_major() {
	echo "$1" | _sed -n 's/[^0-9]*\([0-9][0-9]*\).*/\1/p' | _head -1
}

detect_pm

echo "=========================================="
echo "IR0 — Environment check (deptest)"
echo "OS: ${OS_TYPE}   PROFILE=${PROFILE_DISPLAY}   BOARD=${BOARD:-—}   pm=${PM}"
echo "=========================================="
echo ""

# ---------------------------------------------------------------------------
# Core
# ---------------------------------------------------------------------------
echo "Core toolchain (required):"
echo "--------------------------"
require_cmd "Make" make "make" "make" "make" --version || true
require_cmd "GCC" gcc "build-essential" "gcc" "gcc" --version || true
require_cmd "G++" g++ "build-essential" "gcc" "gcc-c++" --version || true
require_cmd "Linker (ld)" ld "binutils" "binutils" "binutils" --version || true
if command -v ld >/dev/null 2>&1; then
	if ld --help 2>&1 | _grep -q "elf_x86_64\|elf64-x86-64"; then
		ok_line "ld ELF x86-64 target support"
	else
		report_issue "ld" present_but_unusable \
			"  ld is present but does not advertise elf_x86_64 (need binutils for desktop ISO)." \
			"binutils" "binutils" "binutils"
	fi
fi
require_cmd "NASM" nasm "nasm" "nasm" "nasm" -v || true
require_cmd "Python3" python3 "python3" "python" "python3" --version || true
if command -v python3 >/dev/null 2>&1; then
	if python3 -c "import curses" 2>/dev/null; then
		ok_line "python3 curses (menuconfig TUI)"
	else
		report_issue "python3-curses" present_but_unusable \
			"  python3 found but 'import curses' fails (menuconfig TUI)." \
			"libncurses-dev python3" "python" "python3-libs"
	fi
fi
echo ""

# ---------------------------------------------------------------------------
# Desktop
# ---------------------------------------------------------------------------
need_desktop() {
	echo "Desktop / make ir0 (required for ${PROFILE_DISPLAY}):"
	echo "----------------------------------------------------"
	if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
		report_issue "qemu-system-x86_64" required "" \
			"qemu-system-x86" "qemu-system-x86" "qemu-system-x86-core"
	else
		_qv=$(qemu-system-x86_64 --version 2>&1 | _head -1 || true)
		ok_line "QEMU x86_64: ${_qv}"
		_maj=$(parse_major "$_qv")
		if [ -n "$_maj" ] && [ "$_maj" -lt 5 ] 2>/dev/null; then
			report_issue "qemu-system-x86_64" unsupported_version \
				"  Found major ${_maj}; IR0 smokes expect QEMU ≥ 5 (prefer current distro packages)." \
				"qemu-system-x86" "qemu-system-x86" "qemu-system-x86-core"
		fi
		# Usability: machine help must work
		if ! qemu-system-x86_64 -machine help >/dev/null 2>&1; then
			report_issue "qemu-system-x86_64" present_but_unusable \
				"  Binary exists but 'qemu-system-x86_64 -machine help' failed." \
				"qemu-system-x86" "qemu-system-x86" "qemu-system-x86-core"
		fi
	fi

	# KVM: optional acceleration (never fail the gate)
	if [ -e /dev/kvm ]; then
		if [ -r /dev/kvm ] && [ -w /dev/kvm ]; then
			ok_line "KVM (/dev/kvm readable+writable) — optional accel"
		else
			report_issue "/dev/kvm" optional \
				"  Present but not accessible (add user to kvm group). QEMU still runs with TCG."
		fi
	else
		report_issue "KVM" optional \
			"  /dev/kvm absent — OK; QEMU uses software emulation (slower)."
	fi

	if ! command -v grub-mkrescue >/dev/null 2>&1; then
		report_issue "grub-mkrescue" required \
			"  Needed to build kernel-x64.iso / make ir0." \
			"grub-pc-bin grub2-common" "grub" "grub2-tools grub2-tools-extra"
	else
		ok_line "grub-mkrescue"
	fi
	if ! command -v xorriso >/dev/null 2>&1; then
		report_issue "xorriso" required \
			"  Required by grub-mkrescue on most distros." \
			"xorriso" "libisoburn" "xorriso"
	else
		ok_line "xorriso"
	fi

	if ! command -v rustc >/dev/null 2>&1; then
		report_issue "rustc" required \
			"  Rust drivers are linked into the default kernel image.
  Install: https://rustup.rs
    rustup toolchain install nightly
    rustup component add rust-src --toolchain nightly"
	else
		ok_line "rustc: $(rustc --version 2>&1)"
		if ! command -v cargo >/dev/null 2>&1; then
			report_issue "cargo" required "  Required with rustc (rustup)."
		else
			ok_line "cargo: $(cargo --version 2>&1)"
		fi
		if ! command -v rustup >/dev/null 2>&1; then
			report_issue "rustup" required \
				"  Required to manage nightly + rust-src for no_std drivers."
		else
			if rustup toolchain list 2>/dev/null | _grep -q nightly; then
				ok_line "rustup nightly toolchain"
			else
				report_issue "rustup-nightly" required \
					"  Install: rustup toolchain install nightly"
			fi
			if rustup component list --toolchain nightly 2>/dev/null | _grep -q "rust-src (installed)"; then
				ok_line "rust-src (nightly)"
			else
				report_issue "rust-src" required \
					"  Install: rustup component add rust-src --toolchain nightly"
			fi
		fi
	fi

	if [ -f "${KERNEL_ROOT}/.config" ]; then
		ok_line ".config present"
	else
		report_issue ".config" optional \
			"  Missing — next: make defconfig   (or: make ir0_defconfig PROFILE=desktop)"
	fi
	echo ""
}

need_userspace() {
	echo "Userspace musl (required for PROFILE=userspace):"
	echo "-----------------------------------------------"
	if command -v x86_64-linux-musl-gcc >/dev/null 2>&1; then
		ok_line "x86_64-linux-musl-gcc: $(x86_64-linux-musl-gcc --version 2>&1 | _head -1)"
	elif command -v musl-gcc >/dev/null 2>&1; then
		ok_line "musl-gcc: $(musl-gcc --version 2>&1 | _head -1)"
	else
		report_issue "musl-gcc / x86_64-linux-musl-gcc" required \
			"  Or set MUSL_CC=/path/to/x86_64-linux-musl-gcc" \
			"musl-tools" "musl musl-gcc" "musl-gcc"
	fi
	echo ""
}

need_arm64() {
	_which=$1
	echo "ARM64 (${_which} — lab / stub, not a flashable appliance):"
	echo "------------------------------------------------------"
	CROSS="${CROSS_COMPILE_ARM64:-aarch64-linux-gnu-}"
	if command -v "${CROSS}gcc" >/dev/null 2>&1; then
		ok_line "${CROSS}gcc: $(${CROSS}gcc --version 2>&1 | _head -1)"
	else
		report_issue "${CROSS}gcc" required \
			"  Cross compiler for freestanding ARM64 images." \
			"gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu" \
			"aarch64-linux-gnu-gcc aarch64-linux-gnu-binutils" \
			"gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu"
	fi
	if command -v "${CROSS}ld" >/dev/null 2>&1; then
		ok_line "${CROSS}ld"
	else
		report_issue "${CROSS}ld" required "" \
			"binutils-aarch64-linux-gnu" \
			"aarch64-linux-gnu-binutils" \
			"binutils-aarch64-linux-gnu"
	fi
	if ! command -v qemu-system-aarch64 >/dev/null 2>&1; then
		report_issue "qemu-system-aarch64" required \
			"  Needed for ARM smokes (virt / raspi4b when available)." \
			"qemu-system-arm" "qemu-system-aarch64" "qemu-system-aarch64"
	else
		_qv=$(qemu-system-aarch64 --version 2>&1 | _head -1 || true)
		ok_line "qemu-system-aarch64: ${_qv}"
		if ! qemu-system-aarch64 -machine help >/dev/null 2>&1; then
			report_issue "qemu-system-aarch64" present_but_unusable \
				"  Binary exists but '-machine help' failed." \
				"qemu-system-arm" "qemu-system-aarch64" "qemu-system-aarch64"
		elif [ "${_which}" = "hub" ] || [ "${_which}" = "rpi4" ]; then
			if qemu-system-aarch64 -machine help 2>/dev/null | _grep -q '^raspi4b[[:space:]]'; then
				ok_line "QEMU machine raspi4b (emulation lab)"
			else
				report_issue "qemu raspi4b" optional \
					"  No raspi4b machine — smoke-arm64-rpi4-boot will SKIP; image still builds."
			fi
		fi
	fi
	echo ""
}

echo "Optional (warnings only):"
echo "-------------------------"
optional_cmd "MinGW GCC (Windows cross)" x86_64-w64-mingw32-gcc
if command -v python3 >/dev/null 2>&1; then
	if python3 -c "from PIL import Image" 2>/dev/null; then
		ok_line "PIL/Pillow"
	else
		report_issue "Pillow" optional "(optional — menuconfig images)"
	fi
fi
echo ""

NEXT=""
case "${PROFILE}" in
desktop)
	need_desktop
	NEXT="make defconfig && make ir0    # or: make desktop-x86_64"
	;;
userspace)
	need_desktop
	need_userspace
	NEXT="make defconfig && make kernel-x64-userspace.iso"
	;;
hub)
	need_arm64 hub
	NEXT="make hub-rpi4    # UART min lab — not SD-flashable OS"
	;;
watch)
	need_arm64 watch
	NEXT="make watch-rpi5-stub    # compile stub only"
	;;
all)
	need_desktop
	need_userspace
	need_arm64 hub
	NEXT="make help-profiles"
	;;
esac

echo "=========================================="
if [ "$ERRORS" -eq 0 ]; then
	if [ "$WARNINGS" -eq 0 ]; then
		echo "[deptest] OK — profile ${PROFILE_DISPLAY}: all required checks passed"
	else
		echo "[deptest] OK — profile ${PROFILE_DISPLAY}: required OK (${WARNINGS} optional warning(s))"
	fi
	echo "  Next: ${NEXT}"
	exit 0
fi

echo "[deptest] FAIL — profile ${PROFILE_DISPLAY}: ${ERRORS} required issue(s), ${WARNINGS} optional warning(s)"
echo "  Fix the [deptest] blocks above, then:"
echo "  make deptest PROFILE=${PROFILE_DISPLAY}"
echo "  # alias: make check-env PROFILE=${PROFILE_DISPLAY}"
exit 1
