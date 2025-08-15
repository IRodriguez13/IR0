#!/bin/bash

# Script para probar el bootloader standalone
echo "=== Test del Bootloader Standalone ==="

# Compilar bootloader standalone
echo "Compilando bootloader standalone..."
cd arch/x86-64
nasm -f bin asm/boot_x64.asm -o ../../bootloader.bin
cd ../..

# Crear imagen de disco
echo "Creando imagen de disco..."
dd if=/dev/zero of=kernel-x86-64.img bs=1M count=10
dd if=bootloader.bin of=kernel-x86-64.img conv=notrunc

# Probar en QEMU
echo "Probando en QEMU..."
echo "Deberías ver: 'IR0 Bootloader Starting...' y luego 'IKRO' en la pantalla"
timeout 10s qemu-system-x86_64 -drive file=kernel-x86-64.img,format=raw -m 512M -display gtk

echo "Test completado."
