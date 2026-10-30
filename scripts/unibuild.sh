#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
#
# unibuild.sh — compile one or more kernel translation units in isolation.
#
#   scripts/unibuild.sh [-cpp|-rust] FILE [FILE...]
#
# The object lands next to its source (foo.c -> foo.o). C and C++ files are
# compiled in parallel up to the host CPU count; Rust uses a persistent cargo
# workdir so libcore is not rebuilt on every invocation.
#
# This is a developer aid, not the kernel build graph: it never links and never
# writes outside the source tree except under build/unibuild-rust/.

set -eu

usage() {
	cat <<'EOF'
usage: scripts/unibuild.sh [-cpp|-rust] FILE [FILE...]

  -cpp    compile C++ sources (default: C)
  -rust   compile Rust sources through cargo/build-std

C and C++ files compile in parallel. Objects are written next to each source.
Examples:
  make unibuild FILE="fs/tmpfs.c fs/vfs.c"
  make unibuild-cpp FILE=cpp/examples/cpp_example.cpp
  make unibuild-rust FILE=rust/drivers/rust_simple_driver.rs
EOF
}

LANGUAGE=c
while [ $# -gt 0 ]; do
	case "$1" in
	-cpp) LANGUAGE=cpp; shift ;;
	-rust) LANGUAGE=rust; shift ;;
	-win | -win-cpp | -win-rust)
		echo "✗ unibuild: Windows cross-compilation was removed; use WSL." >&2
		exit 2
		;;
	-h | --help) usage; exit 0 ;;
	--) shift; break ;;
	-*) echo "✗ unibuild: unknown option: $1" >&2; usage >&2; exit 2 ;;
	*) break ;;
	esac
done

if [ $# -lt 1 ]; then
	usage >&2
	exit 2
fi

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"

ARCH="${ARCH:-x86-64}"
case "$ARCH" in
x86_64 | amd64) ARCH=x86-64 ;;
aarch64) ARCH=arm64 ;;
x86-64 | arm64) ;;
*)
	echo "✗ unibuild: unsupported ARCH=$ARCH (use x86-64 or arm64)" >&2
	exit 2
	;;
esac

if [ "$ARCH" = arm64 ]; then
	CC="${CC:-aarch64-linux-gnu-gcc}"
	CXX="${CXX:-aarch64-linux-gnu-g++}"
	ASM="${ASM:-aarch64-linux-gnu-gcc}"
	ARCH_FLAGS="-mgeneral-regs-only -DARCH_ARM64"
	ARCH_INC="-I$ROOT/arch/arm64/include"
	ASM_FLAGS="-c"
else
	CC="${CC:-gcc}"
	CXX="${CXX:-g++}"
	ASM="${ASM:-nasm}"
	ARCH_FLAGS="-m64 -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -DARCH_X86_64 -DARCH_X86"
	ARCH_INC="-I$ROOT/arch/x86-64/include"
	ASM_FLAGS="-f elf64 -I$ROOT/"
fi

# Keep these in lockstep with the root Makefile. A unibuild object that is
# compiled with different ABI, optimization or aliasing flags is worse than no
# unibuild object at all.
BASE_FLAGS="-ffreestanding -nostdlib -g -Wall -Wextra -fno-stack-protector -fno-builtin"
OPT_FLAGS="-O2 -fno-strict-aliasing -fno-strict-overflow -fno-omit-frame-pointer -fno-pie -fno-pic"
INCLUDES="-I$ROOT -I$ROOT/includes -I$ROOT/includes/uapi -I$ROOT/ktm/include"
INCLUDES="$INCLUDES -I$ROOT/includes/ir0 -I$ROOT/mm -I$ROOT/arch/common"
INCLUDES="$INCLUDES $ARCH_INC -I$ROOT/arch/arm64 -I$ROOT/kernel -I$ROOT/sched"
INCLUDES="$INCLUDES -I$ROOT/drivers -I$ROOT/fs -I$ROOT/net -I$ROOT/interrupt"
CFLAGS="$BASE_FLAGS $OPT_FLAGS $ARCH_FLAGS -DIR0_DESKTOP $INCLUDES"
CXXFLAGS="$CFLAGS -fno-exceptions -fno-rtti -fno-threadsafe-statics"

JOBS="${UNIBUILD_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}"
case "$JOBS" in
'' | *[!0-9]* | 0) JOBS=1 ;;
esac

compile_one() {
	src=$1
	if [ ! -f "$src" ]; then
		echo "✗ unibuild: file not found: $src" >&2
		return 1
	fi

	dir=$(dirname "$src")
	base=$(basename "$src")
	out="$dir/${base%.*}.o"

	case "$src" in
	*.c)
		if [ "$LANGUAGE" = cpp ]; then
			echo "✗ unibuild: $src is C; use make unibuild" >&2
			return 1
		fi
		echo "  CC      $src"
		$CC $CFLAGS -c "$src" -o "$out"
		;;
	*.cpp | *.cc | *.cxx)
		if [ "$LANGUAGE" != cpp ]; then
			echo "✗ unibuild: $src is C++; use make unibuild-cpp" >&2
			return 1
		fi
		echo "  CXX     $src"
		$CXX $CXXFLAGS -c "$src" -o "$out"
		;;
	*.asm)
		echo "  AS      $src"
		$ASM $ASM_FLAGS "$src" -o "$out"
		;;
	*.s | *.S)
		echo "  AS      $src"
		$CC $CFLAGS -c "$src" -o "$out"
		;;
	*)
		echo "✗ unibuild: unsupported source type: $src" >&2
		return 1
		;;
	esac

	echo "  OBJ     $out"
}

rust_build() {
	src=$1
	if [ ! -f "$src" ]; then
		echo "✗ unibuild: file not found: $src" >&2
		return 1
	fi

	command -v cargo >/dev/null 2>&1 || {
		echo "✗ unibuild: cargo not found; install Rust (https://rustup.rs)" >&2
		return 1
	}
	command -v rustup >/dev/null 2>&1 || {
		echo "✗ unibuild: rustup not found (needed for build-std)" >&2
		return 1
	}
	if ! rustup toolchain list | grep -q '^nightly'; then
		echo "  RUSTUP  installing nightly (build-std)"
		rustup toolchain install nightly >/dev/null 2>&1 || return 1
	fi
	if ! rustup component list --installed 2>/dev/null | grep -q '^rust-src'; then
		echo "  RUSTUP  adding rust-src"
		rustup component add rust-src >/dev/null 2>&1 || return 1
	fi

	work="$ROOT/build/unibuild-rust"
	mkdir -p "$work/src" "$work/.cargo"
	cp "$src" "$work/src/lib.rs"
	cat > "$work/Cargo.toml" <<'EOF'
[package]
name = "ir0_unibuild"
version = "0.1.0"
edition = "2021"

[lib]
crate-type = ["staticlib"]

[profile.dev]
panic = "abort"

[profile.release]
panic = "abort"
opt-level = 2
lto = true
EOF
	cat > "$work/.cargo/config.toml" <<'EOF'
[unstable]
build-std = ["core"]
build-std-features = []
EOF

	# cargo >= 1.9x requires an explicit opt-in for `.json` target specs.
	json_spec=
	if cargo +nightly -Z help 2>/dev/null | grep -q 'json-target-spec'; then
		json_spec=-Zjson-target-spec
	fi

	echo "  RUSTC   $src"
	(
		cd "$work"
		cargo +nightly build --release \
			--target "$ROOT/rust/x86_64-ir0-kernel.json" \
			-Zbuild-std=core $json_spec
	) || return 1

	lib=$(find "$work/target" -path '*/release/libir0_unibuild.a' -type f 2>/dev/null | head -1)
	if [ -z "$lib" ]; then
		lib=$(find "$work/target" -path '*/release/deps/*.a' -type f 2>/dev/null | head -1)
	fi
	if [ -z "$lib" ]; then
		echo "✗ unibuild: cargo succeeded but no static library was produced" >&2
		return 1
	fi

	dir=$(dirname "$src")
	base=$(basename "$src")
	out="$dir/${base%.*}.o"
	cp "$lib" "$out"
	echo "  OBJ     $out"
}

status=0

if [ "$LANGUAGE" = rust ]; then
	for src in "$@"; do
		rust_build "$src" || status=1
	done
	exit "$status"
fi

pids=""
running=0
for src in "$@"; do
	compile_one "$src" &
	pids="$pids $!"
	running=$((running + 1))
	if [ "$running" -ge "$JOBS" ]; then
		for pid in $pids; do
			wait "$pid" || status=1
		done
		pids=""
		running=0
	fi
done
for pid in $pids; do
	wait "$pid" || status=1
done

exit "$status"
