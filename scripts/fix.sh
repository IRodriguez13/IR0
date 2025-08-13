#!/bin/bash
# fix_ir0.sh

echo "Aplicando correcciones IR0..."

# 1. Renombrar directorios
if [ -d "arch/x_64" ]; then
    mv arch/x_64 arch/x86-64
fi

# 2. Corregir Makefile
sed -i 's/x_64/x86-64/g' Makefile
sed -i 's/x86-64)/x86-32)/g' Makefile  # Solo el segundo caso

# 3. Eliminar archivos duplicados
rm -f arch/x86-32/sources/outb_x32.c
rm -f arch/x86-64/sources/outb_64.c
rm -f drivers/timer/timer.c

# 4. Verificar compilación
make ARCH=x86-32 clean all
make ARCH=x86-64 clean all

echo "Correcciones aplicadas!"