#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Build static TinyCC for IR0 and stage rootfs payloads under setup/pid1/fase52_staging/

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TCC_SRC="${TCC_SRC:-/tmp/tinycc-fase52}"
STAGE="${ROOT}/setup/pid1/fase52_staging"
MUSL_CC="${MUSL_CC:-$(command -v x86_64-linux-musl-gcc 2>/dev/null || command -v musl-gcc)}"
MUSL_LIB="${MUSL_LIB:-/usr/lib/x86_64-linux-musl}"
MUSL_INC="${MUSL_INC:-/usr/include/x86_64-linux-musl}"

if [ -z "$MUSL_CC" ]; then
	echo "✗ musl cross compiler not found (set MUSL_CC=...)" >&2
	exit 1
fi

if [ ! -d "$TCC_SRC" ]; then
	echo "  FASE52  Cloning TinyCC into $TCC_SRC"
	git clone --depth 1 https://github.com/TinyCC/tinycc.git "$TCC_SRC"
fi

echo "  FASE52  Building static tcc"
rm -f "${TCC_SRC}/config-extra.mak"
(
	cd "$TCC_SRC"
	make clean >/dev/null 2>&1 || true
	CFLAGS="-static -Os" LDFLAGS="-static" \
		./configure --cc="$MUSL_CC" --prefix=/usr --tccdir=lib/tcc \
		--crtprefix="{B}:/usr/lib"
	make -j"$(nproc)"
)

if [ ! -x "${TCC_SRC}/tcc" ]; then
	echo "✗ tcc build failed: ${TCC_SRC}/tcc missing" >&2
	exit 1
fi

extract_libc_members() {
	local members_file="$1"
	local out_dir="$2"
	local member cnt i

	while IFS= read -r member; do
		[ -n "$member" ] || continue
		cnt=$(ar t "${MUSL_LIB}/libc.a" | grep -xc "$member" || true)
		if [ "$cnt" -gt 1 ]; then
			i=1
			while [ "$i" -le "$cnt" ]; do
				(
					cd "$out_dir"
					ar xN "$i" "${MUSL_LIB}/libc.a" "$member"
					mv "$member" "${member%.*}_${i}.lo"
				)
				i=$((i + 1))
			done
		else
			(
				cd "$out_dir"
				ar x "${MUSL_LIB}/libc.a" "$member"
			)
		fi
	done < "$members_file"
}

build_libc_min_a() {
	local mapfile tmpc tmphelper ardir out

	mapfile="$(mktemp /tmp/f52-libc-map.XXXXXX)"
	tmpc="$(mktemp /tmp/f52-libc-ref.XXXXXX.c)"
	tmphelper="$(mktemp /tmp/f52-libc-helper.XXXXXX.c)"
	ardir="$(mktemp -d /tmp/f52-libc-ar.XXXXXX)"
	out="${STAGE}/usr/lib/libc.a"

	cat >"$tmpc" <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int f52_helper(int x);
int main(int argc, char **argv)
{
	char *p;
	FILE *f;
	char buf[16];

	printf("fmt %d %s %x\n", 42, "ok", 255);
	puts("ir0-libc-min");
	p = malloc(128);
	if (!p)
		return 1;
	memset(p, 'A', 127);
	p[127] = 0;
	free(p);
	if (argc > 1)
		printf("arg %s\n", argv[1]);
	f = fopen("/tmp/f52c.dat", "w");
	if (!f)
		return 2;
	if (fwrite("data42", 1, 6, f) != 6)
		return 3;
	fclose(f);
	f = fopen("/tmp/f52c.dat", "r");
	if (!f)
		return 4;
	if (fread(buf, 1, 6, f) != 6)
		return 5;
	while (fgetc(f) != EOF)
		;
	fclose(f);
	return f52_helper(35);
}
EOF
	cat >"$tmphelper" <<'EOF'
int f52_helper(int x)
{
	return x + 7;
}
EOF
	"$MUSL_CC" -static -o /tmp/f52_libc_map "$tmpc" "$tmphelper" -Wl,-Map="$mapfile"
	grep -oE 'libc\.a\([^)]+\)' "$mapfile" | sed 's/libc.a(\(.*\))/\1/' | sort -u \
		>"${ardir}/members.txt"
	extract_libc_members "${ardir}/members.txt" "$ardir"
	(
		cd "$ardir"
		ar crs "$out" *.lo
	)
	rm -f "$mapfile" "$tmpc" "$tmphelper"
	rm -rf "$ardir"
}

stage_musl_headers_fase52c() {
	local h d

	mkdir -p "${STAGE}/usr/include/bits"
	for h in stdio.h stdlib.h string.h strings.h stdarg.h stddef.h stdint.h \
		stdbool.h errno.h limits.h inttypes.h features.h unistd.h fcntl.h \
		sys/types.h sys/stat.h alloca.h; do
		d="$(dirname "${h}")"
		if [ "$d" != "." ]; then
			mkdir -p "${STAGE}/usr/include/${d}"
		fi
		install -m 0644 "${MUSL_INC}/${h}" "${STAGE}/usr/include/${h}"
	done
	cp -a "${MUSL_INC}/bits/." "${STAGE}/usr/include/bits/"
}

stage_musl_link_runtime() {
	mkdir -p "${STAGE}/usr/lib" "${STAGE}/lib/tcc"
	install -m 0644 "${MUSL_LIB}/crt1.o" "${STAGE}/usr/lib/crt1.o"
	install -m 0644 "${MUSL_LIB}/crti.o" "${STAGE}/usr/lib/crti.o"
	install -m 0644 "${MUSL_LIB}/crtn.o" "${STAGE}/usr/lib/crtn.o"
	build_libc_min_a
	install -m 0644 "${STAGE}/usr/lib/crt1.o" "${STAGE}/lib/tcc/crt1.o"
	install -m 0644 "${STAGE}/usr/lib/crti.o" "${STAGE}/lib/tcc/crti.o"
	install -m 0644 "${STAGE}/usr/lib/crtn.o" "${STAGE}/lib/tcc/crtn.o"
	install -m 0644 "${STAGE}/usr/lib/libc.a" "${STAGE}/lib/tcc/libc.a"
}

FASE52_STAGE_MODE="${FASE52_STAGE:-link}"
echo "  FASE52  Staging rootfs payloads (${FASE52_STAGE_MODE}) -> $STAGE"
rm -rf "$STAGE"
mkdir -p "${STAGE}/bin" "${STAGE}/lib/tcc"

install -m 0755 "${TCC_SRC}/tcc" "${STAGE}/bin/tcc"
install -m 0644 "${TCC_SRC}/libtcc1.a" "${STAGE}/lib/tcc/libtcc1.a"
for obj in runmain.o bt-exe.o bt-log.o; do
	if [ -f "${TCC_SRC}/${obj}" ]; then
		install -m 0644 "${TCC_SRC}/${obj}" "${STAGE}/lib/tcc/${obj}"
	elif [ -f "${TCC_SRC}/lib/${obj}" ]; then
		install -m 0644 "${TCC_SRC}/lib/${obj}" "${STAGE}/lib/tcc/${obj}"
	fi
done

case "$FASE52_STAGE_MODE" in
minimal)
	;;
link)
	stage_musl_link_runtime
	stage_musl_headers_fase52c
	;;
full)
	stage_musl_link_runtime
	mkdir -p "${STAGE}/lib/tcc/include" "${STAGE}/usr/include"
	cp -a "${TCC_SRC}/include/." "${STAGE}/lib/tcc/include/"
	cp -f "${TCC_SRC}/tcclib.h" "${STAGE}/lib/tcc/include/" 2>/dev/null || true
	stage_musl_headers_fase52c
	for obj in bcheck.o; do
		if [ -f "${TCC_SRC}/${obj}" ]; then
			install -m 0644 "${TCC_SRC}/${obj}" "${STAGE}/lib/tcc/${obj}"
		elif [ -f "${TCC_SRC}/lib/${obj}" ]; then
			install -m 0644 "${TCC_SRC}/lib/${obj}" "${STAGE}/lib/tcc/${obj}"
		fi
	done
	;;
*)
	echo "✗ unknown FASE52_STAGE=${FASE52_STAGE_MODE} (minimal|link|full)" >&2
	exit 1
	;;
esac

ROOT_TEST="/tmp/f52-stage-test"
LIBDIR="${ROOT_TEST}/usr/lib"
INCDIR="${ROOT_TEST}/usr/include"
rm -rf "$ROOT_TEST"
cp -a "$STAGE" "$ROOT_TEST"
echo 'int main(){return 0;}' > /tmp/f52_hello.c
if ! "${STAGE}/bin/tcc" -B"${ROOT_TEST}/lib/tcc" -v >/dev/null 2>&1; then
	echo "✗ staged tcc -v failed" >&2
	exit 1
fi
if ! "${STAGE}/bin/tcc" -B"${ROOT_TEST}/lib/tcc" -c -o /tmp/f52_hello.o /tmp/f52_hello.c; then
	echo "✗ staged tcc -c self-test failed" >&2
	exit 1
fi
if ! "${STAGE}/bin/tcc" -B"${ROOT_TEST}/lib/tcc" -nostdlib -run /tmp/f52_hello.c; then
	echo "✗ staged tcc -run self-test failed" >&2
	exit 1
fi
if [ "$FASE52_STAGE_MODE" = "link" ] || [ "$FASE52_STAGE_MODE" = "full" ]; then
	if ! "${STAGE}/bin/tcc" -B"${ROOT_TEST}/lib/tcc" -static \
		-L"${LIBDIR}" -o /tmp/f52_static /tmp/f52_hello.c; then
		echo "✗ staged tcc -static self-test failed" >&2
		exit 1
	fi
	/tmp/f52_static || true
	echo 'int main(){return 42;}' > /tmp/f52_ret42.c
	if ! "${STAGE}/bin/tcc" -B"${ROOT_TEST}/lib/tcc" -static \
		-L"${LIBDIR}" -o /tmp/f52_ret42 /tmp/f52_ret42.c; then
		echo "✗ staged tcc return-42 static self-test failed" >&2
		exit 1
	fi
	ec="$(/tmp/f52_ret42; echo $?)"
	if [ "$ec" != "42" ]; then
		echo "✗ staged return-42 exit=${ec}" >&2
		exit 1
	fi
	echo '#include <stdio.h>
int main(){ puts("hello-tcc"); return 0; }' > /tmp/f52_stdio.c
	if ! "${STAGE}/bin/tcc" -B"${ROOT_TEST}/lib/tcc" -static \
		-I"${INCDIR}" -L"${LIBDIR}" -o /tmp/f52_stdio /tmp/f52_stdio.c; then
		echo "✗ staged tcc stdio static self-test failed" >&2
		exit 1
	fi
	out="$(/tmp/f52_stdio)"
	if [ "$out" != "hello-tcc" ]; then
		echo "✗ staged stdio output mismatch: got '${out}'" >&2
		exit 1
	fi
	echo '#include <stdio.h>
int main(void){ printf("fmt %d %s %x\n", 42, "ok", 255); return 0; }' >/tmp/f52_printf.c
	if ! "${STAGE}/bin/tcc" -B"${ROOT_TEST}/lib/tcc" -static \
		-I"${INCDIR}" -L"${LIBDIR}" -o /tmp/f52_printf /tmp/f52_printf.c; then
		echo "✗ staged tcc printf static self-test failed" >&2
		exit 1
	fi
	out="$(/tmp/f52_printf)"
	if [ "$out" != "fmt 42 ok ff" ]; then
		echo "✗ staged printf output mismatch: got '${out}'" >&2
		exit 1
	fi
	echo '#include <stdlib.h>
#include <string.h>
int main(void){ char *p=malloc(128); if(!p) return 1; memset(p,'\''A'\'',127); p[127]=0; if(p[0]!='\''A'\'') return 2; free(p); return 0; }' >/tmp/f52_malloc.c
	if ! "${STAGE}/bin/tcc" -B"${ROOT_TEST}/lib/tcc" -static \
		-I"${INCDIR}" -L"${LIBDIR}" -o /tmp/f52_malloc /tmp/f52_malloc.c; then
		echo "✗ staged tcc malloc static self-test failed" >&2
		exit 1
	fi
	/tmp/f52_malloc || { echo "✗ staged malloc run failed" >&2; exit 1; }
	echo '#include <stdio.h>
int main(void){ FILE *f=fopen("/tmp/f52c.dat","w"); if(!f) return 1; if(fwrite("data42",1,6,f)!=6) return 2; fclose(f); f=fopen("/tmp/f52c.dat","r"); if(!f) return 3; char b[8]={0}; if(fread(b,1,6,f)!=6) return 4; fclose(f); return (b[0]=='\''d'\''&&b[5]=='\''2'\'')?0:5; }' >/tmp/f52_file.c
	if ! "${STAGE}/bin/tcc" -B"${ROOT_TEST}/lib/tcc" -static \
		-I"${INCDIR}" -L"${LIBDIR}" -o /tmp/f52_file /tmp/f52_file.c; then
		echo "✗ staged tcc file static self-test failed" >&2
		exit 1
	fi
	/tmp/f52_file || { echo "✗ staged file run failed" >&2; exit 1; }
	echo 'int f52_helper(int x); int main(void){ return f52_helper(35); }' >/tmp/f52_multi_a.c
	echo 'int f52_helper(int x){ return x + 7; }' >/tmp/f52_multi_b.c
	if ! "${STAGE}/bin/tcc" -B"${ROOT_TEST}/lib/tcc" -static \
		-I"${INCDIR}" -L"${LIBDIR}" -o /tmp/f52_multi /tmp/f52_multi_a.c /tmp/f52_multi_b.c; then
		echo "✗ staged tcc multi-object static self-test failed" >&2
		exit 1
	fi
	ec="$(/tmp/f52_multi; echo $?)"
	if [ "$ec" != "42" ]; then
		echo "✗ staged multi-object exit=${ec}" >&2
		exit 1
	fi
fi

STAGE_FILES="$(find "$STAGE" -type f | wc -l)"
STAGE_BYTES="$(du -sb "$STAGE" | awk '{print $1}')"
echo "✓ build-tcc-fase52 OK (staging=${STAGE} mode=${FASE52_STAGE_MODE} files=${STAGE_FILES} bytes=${STAGE_BYTES})"
