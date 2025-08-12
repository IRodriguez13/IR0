#!/bin/bash
# ========== PLAN DE CORRECCIONES IR0 KERNEL ==========

echo "1. RENOMBRAR DIRECTORIOS (para consistencia)"
mv arch/x_64 arch/x86-64
mv memory/arch/x_64 memory/arch/x86-64


echo "3. ELIMINAR ARCHIVO DUPLICADO"
rm drivers/timer/timer.c

echo "4. AGREGAR FUNCIONES FALTANTES"
# Agregar cpu_relax() en arch_interface.c
# Agregar paging_set_cpu() en Paging_x86.c
# Agregar print_hex64() en print.c

echo "5. CORREGIR MAKEFILE PRINCIPAL"
# Cambiar x_64 → x86-64 en todas las referencias

echo "6. PROBAR COMPILACIÓN"
make ARCH=x86-32 clean all
make ARCH=x86-64 clean all

echo "¡Correcciones completadas!"