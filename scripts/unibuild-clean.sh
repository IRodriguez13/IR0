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

if [ -f "$OUTPUT_FILE" ]; then
    rm -f "$OUTPUT_FILE"
    echo "  CLEAN   $OUTPUT_FILE"
else
    echo "  CLEAN   $OUTPUT_FILE (not found)"
fi

