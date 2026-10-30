#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
#
# run_qemu.sh — boot an IR0 artifact that is already on disk.
#
# This script never invokes make and never depends on ISD. It resolves the
# product from ARCH/BOARD, validates the QEMU machine, and then execs QEMU.
#
#   scripts/run_qemu.sh gui|console|debug|fullscreen|gdb
#
# Environment:
#   ARCH=x86-64|arm64       target architecture (default: .config / x86-64)
#   BOARD=qemu-virt|rpi3|rpi4|rpi5   ARM64 board (default: qemu-virt)
#   IR0_PRODUCT=path        explicit product (default: per ARCH/BOARD)
#   DISK=path|none          x86 raw disk (default: disk.img when present)
#   QEMU=path               explicit QEMU binary
#   IR0_MEM=512M|1G|...     guest memory (default: 512M x86, 1G ARM64)
#   IR0_NET=0               disable the default user-mode NIC
#   IR0_DENNIS_9P=0         disable the host tree 9p share
#   IR0_AUDIO=1             enable SB16/AdLib (x86 only; default off)
#   IR0_QEMU_EXTRA="..."    extra arguments appended verbatim
#
# Keep this POSIX: contributors run it on Linux, BSD and macOS hosts.

set -eu

usage() {
	cat <<'EOF'
usage: scripts/run_qemu.sh [gui|console|debug|fullscreen|gdb]

Boots the artifact already built for ARCH/BOARD. It never invokes make.
Build it first with:
  make ir0 x86-64
  make ir0 arm64
  make ir0 arm64 rpi4
EOF
}

MODE="${1:-gui}"
case "$MODE" in
gui | console | debug | fullscreen | gdb) ;;
-h | --help | help)
	usage
	exit 0
	;;
*)
	usage >&2
	exit 2
	;;
esac

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"

ARCH="${ARCH:-}"
BOARD="${BOARD:-}"
PRODUCT="${IR0_PRODUCT:-}"
DISK="${DISK:-}"

# Resolve the architecture from .config when the caller did not pass one.
if [ -z "$ARCH" ]; then
	if [ -f "$ROOT/.config" ] && grep -q '^CONFIG_ARCH_ARM64=y' "$ROOT/.config"; then
		ARCH=arm64
	else
		ARCH=x86-64
	fi
fi
case "$ARCH" in
x86_64 | amd64) ARCH=x86-64 ;;
aarch64) ARCH=arm64 ;;
x86-64 | arm64) ;;
*)
	echo "✗ run: unsupported ARCH=$ARCH (use x86-64 or arm64)" >&2
	exit 2
	;;
esac

# BOARD is only meaningful on ARM64. Keep qemu-virt as the default so
# `make run ARCH=arm64` matches `make ir0 ARCH=arm64`.
if [ "$ARCH" = arm64 ]; then
	BOARD="${BOARD:-qemu-virt}"
	case "$BOARD" in
	qemu-virt | rpi3 | rpi4 | rpi5) ;;
	*)
		echo "✗ run: unsupported BOARD=$BOARD" >&2
		echo "  valid: qemu-virt, rpi3, rpi4, rpi5" >&2
		exit 2
		;;
	esac
else
	BOARD=""
fi

# Product resolution. Board images are raw kernel8.img files for QEMU's
# raspi machines; the ELF twin remains the link product.
if [ -z "$PRODUCT" ]; then
	if [ "$ARCH" = arm64 ]; then
		case "$BOARD" in
		rpi3 | rpi4 | rpi5) PRODUCT="kernel-arm64-$BOARD-min.bin" ;;
		*) PRODUCT="kernel-arm64.bin" ;;
		esac
	else
		PRODUCT="kernel-x64.iso"
	fi
fi
case "$PRODUCT" in
/*) ;;
*) PRODUCT="$ROOT/$PRODUCT" ;;
esac

if [ "$ARCH" = arm64 ] && [ "$BOARD" != qemu-virt ]; then
	board_img="$ROOT/build/arm64-$BOARD/kernel8.img"
	if [ -f "$board_img" ]; then
		PRODUCT="$board_img"
	fi
fi

if [ ! -f "$PRODUCT" ]; then
	echo "✗ run: product not built: $PRODUCT" >&2
	if [ "$ARCH" = arm64 ]; then
		echo "  build it: make ir0 arm64 ${BOARD:+$BOARD}" >&2
	else
		echo "  build it: make ir0 x86-64" >&2
	fi
	exit 1
fi

# Disk is optional. Auto-attach disk.img when it exists; DISK=none opts out.
NO_DISK=0
case "$DISK" in
none | -) DISK=""; NO_DISK=1 ;;
esac
if [ -z "$DISK" ] && [ "$NO_DISK" = 0 ] && [ "$ARCH" = x86-64 ] && [ -f "$ROOT/disk.img" ]; then
	DISK="$ROOT/disk.img"
fi
if [ -n "$DISK" ] && [ ! -f "$DISK" ]; then
	echo "✗ run: disk image not found: $DISK" >&2
	exit 1
fi

if [ -n "${QEMU:-}" ]; then
	qemu=$QEMU
elif [ "$ARCH" = arm64 ]; then
	qemu=qemu-system-aarch64
else
	qemu=qemu-system-x86_64
fi
if ! command -v "$qemu" >/dev/null 2>&1; then
	echo "✗ run: QEMU not found: $qemu" >&2
	echo "  install qemu-system-$([ "$ARCH" = arm64 ] && echo aarch64 || echo x86_64)" >&2
	exit 1
fi

MEM="${IR0_MEM:-}"
if [ -z "$MEM" ]; then
	if [ "$ARCH" = arm64 ]; then MEM=1G; else MEM=512M; fi
fi

# Validate the machine before QEMU prints a wall of text.
if [ "$ARCH" = arm64 ]; then
	case "$BOARD" in
	qemu-virt) MACHINE=virt ;;
	rpi3) MACHINE=raspi3b ;;
	rpi4) MACHINE=raspi4b ;;
	rpi5) MACHINE=raspi5b ;;
	esac
	if ! "$qemu" -machine help 2>/dev/null | awk '{print $1}' | grep -qx "$MACHINE"; then
		echo "✗ run: $qemu has no '$MACHINE' machine" >&2
		echo "  available ARM machines:" >&2
		"$qemu" -machine help 2>/dev/null | awk '{print "    " $1}' >&2 || true
		exit 1
	fi
fi

set -- -m "$MEM"

if [ "$ARCH" = arm64 ]; then
	set -- "$@" -M "$MACHINE" -kernel "$PRODUCT" -no-reboot
	if [ "$MACHINE" = virt ]; then
		set -- "$@" -cpu cortex-a57
	fi
	case "$MODE" in
	gdb)
		set -- "$@" -serial stdio -display none -s -S
		;;
	*)
		# The ARM64 early console is PL011 serial; there is no framebuffer yet.
		set -- "$@" -nographic
		;;
	esac
else
	set -- "$@" -cdrom "$PRODUCT" -no-reboot \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04
	if [ -n "$DISK" ]; then
		set -- "$@" -drive "file=$DISK,format=raw,if=ide,index=0"
	fi
	case "$MODE" in
	gui)
		set -- "$@" -display gtk -serial stdio
		;;
	console)
		set -- "$@" -nographic
		;;
	fullscreen)
		set -- "$@" -display gtk,zoom-to-fit=on,full-screen=on -serial stdio
		;;
	debug)
		set -- "$@" -display none -serial stdio -no-shutdown \
			-monitor telnet:127.0.0.1:1234,server,nowait \
			-d guest_errors,int -D "$ROOT/qemu_debug.log"
		;;
	gdb)
		set -- "$@" -display none -serial stdio -no-shutdown -s -S
		;;
	esac
	if [ "${IR0_NET:-1}" != 0 ]; then
		set -- "$@" -netdev user,id=net0 -device rtl8139,netdev=net0
	fi
	if [ "${IR0_DENNIS_9P:-1}" != 0 ]; then
		set -- "$@" \
			-fsdev "local,id=dennisfs,path=$ROOT,security_model=none" \
			-device virtio-9p-pci,fsdev=dennisfs,mount_tag=dennis,disable-modern=on
	fi
	if [ "${IR0_AUDIO:-0}" = 1 ]; then
		set -- "$@" -audiodev "${QEMU_AUDIO_BACKEND:-pa},id=snd0" \
			-device sb16,audiodev=snd0 -device adlib,audiodev=snd0
	fi
fi

# Extra arguments are intentionally word-split: callers use this for ad-hoc
# QEMU flags, not for paths with spaces.
if [ -n "${IR0_QEMU_EXTRA:-}" ]; then
	# shellcheck disable=SC2086
	set -- "$@" $IR0_QEMU_EXTRA
fi

if [ "${IR0_QUIET:-0}" != 1 ]; then
	echo "  RUN     $qemu $MODE ARCH=$ARCH${BOARD:+ BOARD=$BOARD}"
	echo "          product: $PRODUCT"
	[ -n "$DISK" ] && echo "          disk:    $DISK"
fi

exec "$qemu" "$@"
