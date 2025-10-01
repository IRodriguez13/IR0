#!/bin/bash
# Verify that all .o files listed in Makefile.minimal have corresponding source files

echo "🔍 Verifying object files in Makefile.minimal..."
echo ""

MISSING=0
FOUND=0

# Extract all .o files from Makefile.minimal
OBJECTS=$(grep -E '\.o($| \\)' Makefile.minimal | grep -v '^#' | sed 's/\\$//' | awk '{print $1}' | sort -u)

for obj in $OBJECTS; do
    # Remove .o extension
    base="${obj%.o}"
    
    # Check if source file exists (.c or .asm)
    if [ -f "${base}.c" ] || [ -f "${base}.asm" ]; then
        echo "✓ $obj"
        ((FOUND++))
    else
        echo "✗ $obj (source not found: ${base}.c or ${base}.asm)"
        ((MISSING++))
    fi
done

echo ""
echo "════════════════════════════════════════════════════"
echo "Found:   $FOUND"
echo "Missing: $MISSING"
echo "════════════════════════════════════════════════════"

if [ $MISSING -eq 0 ]; then
    echo "✅ All object files have corresponding source files!"
    exit 0
else
    echo "❌ Some object files are missing source files"
    exit 1
fi
