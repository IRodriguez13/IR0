#!/bin/bash
# Rename directories
mv arch/x_64 arch/x86-64

# Update all Makefiles
find . -name "Makefile" -exec sed -i 's/x_64/x86-64/g' {} \;
find . -name "*.c" -exec sed -i 's/x_64/x86-64/g' {} \;
find . -name "*.h" -exec sed -i 's/x_64/x86-64/g' {} \;