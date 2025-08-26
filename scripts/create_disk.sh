#!/bin/bash

# Script para crear un disco virtual con Minix FS para IR0 Kernel
# Autor: IR0 Kernel Team

echo "🔧 Creando disco virtual para IR0 Kernel..."

# Configuración del disco
DISK_SIZE="100M"
DISK_FILE="disk.img"
MOUNT_POINT="/tmp/ir0_disk_mount"

# Crear archivo de disco
echo "📁 Creando archivo de disco de $DISK_SIZE..."
dd if=/dev/zero of=$DISK_FILE bs=1M count=100 2>/dev/null

# Crear tabla de particiones
echo "🔧 Configurando tabla de particiones..."
echo -e "n\np\n1\n\n\nw" | fdisk $DISK_FILE >/dev/null 2>&1

# Crear sistema de archivos Minix
echo "📂 Formateando con Minix FS..."
# Usar loopback device para acceder al disco
sudo losetup -f $DISK_FILE
LOOP_DEVICE=$(losetup -j $DISK_FILE | cut -d: -f1)

# Crear partición
sudo fdisk $LOOP_DEVICE <<EOF >/dev/null 2>&1
n
p
1


w
EOF

# Formatear con Minix FS
sudo mkfs.minix -1 $LOOP_DEVICE 2>/dev/null

# Crear directorio de montaje
sudo mkdir -p $MOUNT_POINT

# Montar disco
echo "🔗 Montando disco..."
sudo mount $LOOP_DEVICE $MOUNT_POINT

# Crear estructura básica de directorios
echo "📁 Creando estructura de directorios..."
sudo mkdir -p $MOUNT_POINT/{bin,etc,home,usr,var,tmp}
sudo mkdir -p $MOUNT_POINT/usr/{bin,lib,include}
sudo mkdir -p $MOUNT_POINT/var/{log,tmp}

# Crear archivos de prueba
echo "📄 Creando archivos de prueba..."
echo "IR0 Kernel Test File" | sudo tee $MOUNT_POINT/test.txt >/dev/null
echo "Hello from IR0 Kernel!" | sudo tee $MOUNT_POINT/hello.txt >/dev/null

# Crear archivo de configuración
sudo tee $MOUNT_POINT/etc/ir0.conf >/dev/null <<EOF
# IR0 Kernel Configuration
KERNEL_VERSION=1.0.0
FILESYSTEM=minix
BOOT_MODE=normal
EOF

# Desmontar disco
echo "🔗 Desmontando disco..."
sudo umount $MOUNT_POINT
sudo losetup -d $LOOP_DEVICE

# Limpiar
sudo rmdir $MOUNT_POINT

echo "✅ Disco virtual creado exitosamente: $DISK_FILE"
echo "📊 Tamaño del disco: $(du -h $DISK_FILE | cut -f1)"
echo ""
echo "🚀 Para usar el disco con QEMU:"
echo "   qemu-system-x86_64 -cdrom kernel-x86-64-desktop.iso -hda $DISK_FILE -m 512M"
echo ""
echo "📁 Contenido del disco:"
echo "   - /bin, /etc, /home, /usr, /var, /tmp"
echo "   - test.txt, hello.txt"
echo "   - /etc/ir0.conf"
