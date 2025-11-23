#!/bin/sh

# Parse flags
BUILD_MODE="native"
LANGUAGE="c"

while [ $# -gt 0 ]; do
    case "$1" in
        -win)
            BUILD_MODE="win"
            shift
            ;;
        -rust)
            LANGUAGE="rust"
            shift
            ;;
        -cpp)
            LANGUAGE="cpp"
            shift
            ;;
        *)
            break
            ;;
    esac
done

# Check if files were provided after flags
if [ $# -lt 1 ]; then
    echo "Usage: $0 [-win] [-rust|-cpp] <source_file1> [source_file2] [source_file3] ..."
    echo "       Compiles one or more source files independently"
    echo ""
    echo "Flags:"
    echo "  -win   Cross-compile for Windows using MinGW"
    echo "  -rust  Compile Rust source files (future support)"
    echo "  -cpp   Compile C++ source files (future support)"
    echo ""
    echo "Flags can be combined:"
    echo "  -win -cpp    Cross-compile C++ for Windows"
    echo "  -win -rust   Cross-compile Rust for Windows (future)"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
KERNEL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$KERNEL_ROOT" || exit 1

SOURCE_FILES="$@"

# Detect OS and compiler based on BUILD_MODE and LANGUAGE
if [ "$LANGUAGE" = "rust" ]; then
    # Rust compilation (future support)
    echo "Error: Rust support is not yet implemented"
    echo "This feature is planned for future releases"
    if [ "$BUILD_MODE" = "win" ]; then
        echo "Target: Windows cross-compilation"
    fi
    exit 1
elif [ "$LANGUAGE" = "cpp" ]; then
    # C++ compilation (future support)
    echo "Error: C++ support is not yet implemented"
    echo "This feature is planned for future releases"
    if [ "$BUILD_MODE" = "win" ]; then
        echo "Target: Windows cross-compilation"
        echo "Will use: x86_64-w64-mingw32-g++ (when implemented)"
    else
        echo "Will use: g++ or clang++ (when implemented)"
    fi
    exit 1
elif [ "$BUILD_MODE" = "win" ]; then
    # Cross-compilation to Windows using MinGW (C language)
    echo "==> Cross-compiling C for Windows with MinGW"
    if command -v x86_64-w64-mingw32-gcc > /dev/null 2>&1; then
        CC="x86_64-w64-mingw32-gcc"
        ASM="nasm"
        ASMFLAGS="-f win64"
        CFLAGS="-m64 -ffreestanding -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -nostdlib -lgcc -g -Wall -Wextra -fno-stack-protector -fno-builtin"
        CFLAGS="$CFLAGS -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast"
        export USE_MINGW=1
    else
        echo "Error: MinGW cross-compiler not found"
        echo "Install with: sudo apt-get install mingw-w64"
        exit 1
    fi
elif [ "$OS" = "Windows_NT" ] || [ -n "$WINDIR" ]; then
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
elif [ -n "$USE_MINGW" ] && command -v x86_64-w64-mingw32-gcc > /dev/null 2>&1; then
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

# Process each source file
EXIT_CODE=0
for SOURCE_FILE in $SOURCE_FILES; do
    if [ ! -f "$SOURCE_FILE" ]; then
        echo "Error: File not found: $SOURCE_FILE"
        EXIT_CODE=1
        continue
    fi

    EXT="${SOURCE_FILE##*.}"
    BASENAME=$(basename "$SOURCE_FILE" ".$EXT")
    DIRNAME=$(dirname "$SOURCE_FILE")
    OUTPUT_FILE="$DIRNAME/$BASENAME.o"

    case "$EXT" in
        c)
            echo "  CC      $SOURCE_FILE"
            $CC $CFLAGS -c "$SOURCE_FILE" -o "$OUTPUT_FILE"
            if [ $? -eq 0 ]; then
                echo "  OBJ     $OUTPUT_FILE"
            else
                EXIT_CODE=1
            fi
            ;;
        asm)
            echo "  ASM     $SOURCE_FILE"
            $ASM $ASMFLAGS "$SOURCE_FILE" -o "$OUTPUT_FILE"
            if [ $? -eq 0 ]; then
                echo "  OBJ     $OUTPUT_FILE"
            else
                EXIT_CODE=1
            fi
            ;;
        *)
            echo "Error: Unsupported file type: .$EXT"
            echo "Supported types: .c, .asm"
            EXIT_CODE=1
            ;;
    esac
done

exit $EXIT_CODE

