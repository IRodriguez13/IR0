#!/bin/sh

if [ $# -lt 1 ]; then
    echo "Usage: $0 <source_file>"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
KERNEL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$KERNEL_ROOT" || exit 1

SOURCE_FILE="$1"

if [ ! -f "$SOURCE_FILE" ]; then
    echo "Error: File not found: $SOURCE_FILE"
    exit 1
fi

EXT="${SOURCE_FILE##*.}"
BASENAME=$(basename "$SOURCE_FILE" ".$EXT")
DIRNAME=$(dirname "$SOURCE_FILE")
OUTPUT_FILE="$DIRNAME/$BASENAME.o"

# Detect OS and compiler
if [ "$OS" = "Windows_NT" ] || [ -n "$WINDIR" ]; then
    # Native Windows - use GCC (MSYS2/MinGW) which supports ELF
    CC="gcc"
    ASM="nasm"
    # Try ELF format first (kernel needs ELF)
    ASMFLAGS="-f elf64"
    CFLAGS="-m64 -ffreestanding -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -nostdlib -lgcc -g -Wall -Wextra -fno-stack-protector -fno-builtin"
    # Test if ELF format works, fallback to win64 if not
    if ! nasm -f elf64 /dev/null 2>/dev/null && ! nasm -f elf64 nul 2>/dev/null; then
        ASMFLAGS="-f win64"
    fi
elif [ -n "$USE_MINGW" ] && command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
    # Explicit cross-compilation from Linux using MinGW-w64
    CC="x86_64-w64-mingw32-gcc"
    ASM="nasm"
    ASMFLAGS="-f win64"
    CFLAGS="-m64 -ffreestanding -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -nostdlib -lgcc -g -Wall -Wextra -fno-stack-protector -fno-builtin"
    CFLAGS="$CFLAGS -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast"
else
    # Native Linux (default)
    CC="gcc"
    ASM="nasm"
    ASMFLAGS="-f elf64"
    CFLAGS="-m64 -ffreestanding -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -nostdlib -lgcc -g -Wall -Wextra -fno-stack-protector -fno-builtin"
fi

CFLAGS="$CFLAGS -I./includes -I./ -I./includes/ir0"
CFLAGS="$CFLAGS -I./arch/common -I./arch/x86-64/include"
CFLAGS="$CFLAGS -I./include -I./kernel"
CFLAGS="$CFLAGS -I./drivers -I./fs -I./interrupt"

case "$EXT" in
    c)
        echo "  CC      $SOURCE_FILE"
        $CC $CFLAGS -c "$SOURCE_FILE" -o "$OUTPUT_FILE"
        if [ $? -eq 0 ]; then
            echo "  OBJ     $OUTPUT_FILE"
        else
            exit 1
        fi
        ;;
    asm)
        echo "  ASM     $SOURCE_FILE"
        $ASM $ASMFLAGS "$SOURCE_FILE" -o "$OUTPUT_FILE"
        if [ $? -eq 0 ]; then
            echo "  OBJ     $OUTPUT_FILE"
        else
            exit 1
        fi
        ;;
    *)
        echo "Error: Unsupported file type: .$EXT"
        echo "Supported types: .c, .asm"
        exit 1
        ;;
esac

