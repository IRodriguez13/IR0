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
    # Rust compilation with cargo (build-std for libcore)
    echo "===> Compiling Rust for IR0 Kernel"
    
    # Check if rustc and cargo are available
    if ! command -v rustc > /dev/null 2>&1; then
        echo "Error: Rust compiler (rustc) not found"
        echo "Install Rust: curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh"
        exit 1
    fi
    
    if ! command -v cargo > /dev/null 2>&1; then
        echo "Error: Cargo not found"
        echo "Install Rust toolchain with cargo"
        exit 1
    fi
    
    # Check for rust-src component (required for build-std)
    if ! rustup component list | grep -q "rust-src (installed)"; then
        echo "Installing rust-src component (required for build-std)..."
        rustup component add rust-src
        if [ $? -ne 0 ]; then
            echo "Error: Failed to install rust-src"
            echo "Run: rustup component add rust-src"
            exit 1
        fi
    fi
    
    RUST_TARGET="$KERNEL_ROOT/rust/x86_64-ir0-kernel.json"
    
    # Note: We use cargo instead of rustc to enable build-std
    # This compiles libcore from source for our custom target
    USE_CARGO=1
    
    if [ "$BUILD_MODE" = "win" ]; then
        echo "Note: Cross-compiling Rust to Windows (experimental)"
    fi
elif [ "$LANGUAGE" = "cpp" ]; then
    # C++ compilation
    echo "===> Compiling C++ for IR0 Kernel"
    
    if [ "$BUILD_MODE" = "win" ]; then
        # Windows cross-compilation
        if command -v x86_64-w64-mingw32-g++ > /dev/null 2>&1; then
            CXX="x86_64-w64-mingw32-g++"
            CXXFLAGS="-m64 -ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics"
            CXXFLAGS="$CXXFLAGS -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2"
            CXXFLAGS="$CXXFLAGS -nostdlib -lgcc -g -Wall -Wextra -fno-stack-protector -fno-builtin"
            CXXFLAGS="$CXXFLAGS -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast"
            echo "Using MinGW G++ for Windows cross-compilation"
        else
            echo "Error: MinGW G++ cross-compiler not found"
            echo "Install with: sudo apt-get install mingw-w64"
            exit 1
        fi
    else
        # Native compilation
        if command -v g++ > /dev/null 2>&1; then
            CXX="g++"
        elif command -v clang++ > /dev/null 2>&1; then
            CXX="clang++"
        else
            echo "Error: C++ compiler not found (g++ or clang++)"
            exit 1
        fi
        
        # C++ kernel flags (matching unibuild expectations)
        CXXFLAGS="-m64 -ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics"
        CXXFLAGS="$CXXFLAGS -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2"
        CXXFLAGS="$CXXFLAGS -nostdlib -lgcc -g -Wall -Wextra -fno-stack-protector -fno-builtin"
    fi
    
    # Add include paths for C++
    CXXFLAGS="$CXXFLAGS -I$KERNEL_ROOT/cpp/include"
    CXXFLAGS="$CXXFLAGS -I$KERNEL_ROOT/includes -I$KERNEL_ROOT -I$KERNEL_ROOT/includes/ir0"
    CXXFLAGS="$CXXFLAGS -I$KERNEL_ROOT/arch/common -I$KERNEL_ROOT/arch/x86-64/include"
    CXXFLAGS="$CXXFLAGS -I$KERNEL_ROOT/include -I$KERNEL_ROOT/kernel"
    CXXFLAGS="$CXXFLAGS -I$KERNEL_ROOT/drivers -I$KERNEL_ROOT/fs -I$KERNEL_ROOT/interrupt"
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
        cpp)
            if [ -z "$CXX" ]; then
                echo "Error: C++ compiler not configured"
                echo "Use: make unibuild -cpp <file.cpp>"
                EXIT_CODE=1
            else
                echo "  CXX     $SOURCE_FILE"
                $CXX $CXXFLAGS -c "$SOURCE_FILE" -o "$OUTPUT_FILE"
                if [ $? -eq 0 ]; then
                    echo "  OBJ     $OUTPUT_FILE"
                else
                    EXIT_CODE=1
                fi
            fi
            ;;
        rs)
            if [ -z "$USE_CARGO" ]; then
                echo "Error: Rust compiler not configured"
                echo "Use: make unibuild-rust <file.rs>"
                EXIT_CODE=1
            else
                echo "  RUSTC   $SOURCE_FILE"
                
                # Check for nightly toolchain (required for build-std)
                if ! rustup toolchain list | grep -q "nightly"; then
                    echo "Installing nightly toolchain (required for build-std)..."
                    rustup toolchain install nightly
                fi
                
                # Create temporary cargo project for build-std
                TEMP_DIR=$(mktemp -d)
                TEMP_PROJECT="$TEMP_DIR/temp_rust"
                
                # Create project structure manually
                mkdir -p "$TEMP_PROJECT/src"
                mkdir -p "$TEMP_PROJECT/.cargo"
                
                # Copy source file
                cp "$KERNEL_ROOT/$SOURCE_FILE" "$TEMP_PROJECT/src/lib.rs"
                
                # Get absolute path for target
                TARGET_PATH=$(cd "$KERNEL_ROOT" && pwd)/rust/x86_64-ir0-kernel.json
                
                # Create Cargo.toml
                cat > "$TEMP_PROJECT/Cargo.toml" << 'EOF'
[package]
name = "temp_rust_build"
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

                # Create .cargo/config.toml for build-std
                cat > "$TEMP_PROJECT/.cargo/config.toml" << EOF
[unstable]
build-std = ["core"]
build-std-features = []
EOF

                # Build with cargo nightly
                cd "$TEMP_PROJECT"
                OUTPUT=$(cargo +nightly build --release --target "$TARGET_PATH" -Zbuild-std=core 2>&1)
                BUILD_SUCCESS=$?
                
                if [ $BUILD_SUCCESS -eq 0 ]; then
                    # The target filename is complex, find the .a file
                    TARGET_DIR="$TEMP_PROJECT/target/x86_64-ir0-kernel/release"
                    RUST_LIB=$(find "$TARGET_DIR" -name "libtemp_rust_build.a" 2>/dev/null | head -1)
                    
                    if [ -f "$RUST_LIB" ]; then
                        # Copy the static library as .o
                        cd "$KERNEL_ROOT"
                        cp "$RUST_LIB" "$OUTPUT_FILE"
                        echo "  OBJ     $OUTPUT_FILE"
                    else
                        # Try to find any .a file
                        RUST_LIB=$(find "$TARGET_DIR" -name "*.a" 2>/dev/null | head -1)
                        if [ -f "$RUST_LIB" ]; then
                            cd "$KERNEL_ROOT"
                            cp "$RUST_LIB" "$OUTPUT_FILE"
                            echo "  OBJ     $OUTPUT_FILE"
                        else
                            echo "Error: Build succeeded but no output library found"
                            echo "Looked in: $TARGET_DIR"
                            EXIT_CODE=1
                        fi
                    fi
                else
                    echo "$OUTPUT" | grep "error"
                    EXIT_CODE=1
                fi
                
                # Cleanup
                cd "$KERNEL_ROOT"
                rm -rf "$TEMP_DIR"
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
            echo "Supported types: .c, .cpp, .rs, .asm"
            EXIT_CODE=1
            ;;
    esac
done

exit $EXIT_CODE

