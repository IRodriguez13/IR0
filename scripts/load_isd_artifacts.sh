# SPDX-License-Identifier: GPL-3.0-only
# Generate ISD artifact cache for make -include (avoids $(shell) newline collapse).
set -euo pipefail

ISD_ROOT="${1:?ISD root}"
ARCH="${2:-x86_64}"
PROFILE="${3:-minimal}"
IR0_ROOT="${4:?IR0 root}"
OUT="${5:?output .mk path}"
PREFIX="${6:-}"
disk_var="IR0_ISD_DISK"
home_var="IR0_ISD_HOME_DISK"
ext2_var="IR0_ISD_DISK_EXT2"
rootfs_var="IR0_ISD_ROOTFS"
stamp_var="ISD_ROOTFS_STAMP"
req_var="ISD_REQUIRES_HOME_DISK"
pack_var="ISD_ROOT_FS"
variant_var="ISD_VARIANT_ID"
if [ "$PREFIX" = "KMANG_" ]; then
	disk_var="KMANG_ISD_DISK"
	home_var="KMANG_ISD_HOME_DISK"
	ext2_var="KMANG_ISD_DISK_EXT2"
	rootfs_var="KMANG_ISD_ROOTFS"
	stamp_var="KMANG_ISD_ROOTFS_STAMP"
	req_var="KMANG_REQUIRES_HOME_DISK"
	pack_var="KMANG_ROOT_FS"
	variant_var="KMANG_VARIANT_ID"
fi

if [ ! -f "${ISD_ROOT}/Makefile" ]; then
	exit 0
fi

mkdir -p "$(dirname "$OUT")"
tmp="${OUT}.tmp.$$"

if ! make -C "$ISD_ROOT" IR0_ROOT="$IR0_ROOT" ARCH="$ARCH" PROFILE="$PROFILE" \
	-s print-artifacts-mk >"$tmp" 2>/dev/null; then
	rm -f "$tmp"
	exit 0
fi

while IFS= read -r line; do
	case "$line" in
	''|\#*) continue ;;
	ROOT_DISK:=*)
		printf '%s\n' "${disk_var} := ${line#ROOT_DISK:=}"
		;;
	ROOT_DISK_EXT2:=*)
		printf '%s\n' "${ext2_var} := ${line#ROOT_DISK_EXT2:=}"
		;;
	HOME_DISK:=*)
		printf '%s\n' "${home_var} := ${line#HOME_DISK:=}"
		;;
	ROOTFS:=*)
		printf '%s\n' "${rootfs_var} := ${line#ROOTFS:=}"
		;;
	ROOTFS_STAMP:=*)
		printf '%s\n' "${stamp_var} := ${line#ROOTFS_STAMP:=}"
		;;
	REQUIRES_HOME_DISK:=*)
		printf '%s\n' "${req_var} := ${line#REQUIRES_HOME_DISK:=}"
		;;
	ROOTFS_PACK:=*)
		printf '%s\n' "${pack_var} := ${line#ROOTFS_PACK:=}"
		;;
	ROOT_FS:=*)
		printf '%s\n' "${pack_var} := ${line#ROOT_FS:=}"
		;;
	VARIANT_ID:=*)
		printf '%s\n' "${variant_var} := ${line#VARIANT_ID:=}"
		;;
	esac
done <"$tmp" >"${tmp}.map"
mv "${tmp}.map" "$OUT"
rm -f "$tmp"
