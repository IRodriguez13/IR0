#!/bin/bash
# Script para configurar TAP networking para IR0 Kernel
# Uso: sudo ./scripts/setup_tap.sh

set -e

TAP_IF="tap0"
BRIDGE_IF="br0"
PHYS_IF="wlx90de80a5609e"  # Cambia esto a tu interfaz de red activa

echo "🔧 Configurando TAP networking para IR0 Kernel..."

# Verificar si TUN/TAP está disponible
if [ ! -c /dev/net/tun ]; then
    echo "❌ TUN/TAP device no disponible. Cargando módulo..."
    modprobe tun
fi

# Verificar si la interfaz física existe
if ! ip link show "$PHYS_IF" &>/dev/null; then
    echo "❌ Error: Interfaz $PHYS_IF no encontrada"
    echo "💡 Interfaces disponibles:"
    ip link show | grep -E "^[0-9]+:" | awk '{print $2}' | sed 's/:$//'
    exit 1
fi

# Limpiar configuraciones anteriores si existen
if ip link show "$BRIDGE_IF" &>/dev/null; then
    echo "⚠️  Bridge $BRIDGE_IF ya existe, limpiando..."
    ip link set "$BRIDGE_IF" down 2>/dev/null || true
    ip link delete "$BRIDGE_IF" 2>/dev/null || true
fi

if ip link show "$TAP_IF" &>/dev/null; then
    echo "⚠️  TAP $TAP_IF ya existe, limpiando..."
    ip link set "$TAP_IF" down 2>/dev/null || true
    ip link delete "$TAP_IF" 2>/dev/null || true
fi

# 1. Crear bridge
echo "📡 Creando bridge $BRIDGE_IF..."
ip link add "$BRIDGE_IF" type bridge

# 2. Crear TAP
echo "🔌 Creando interfaz TAP $TAP_IF..."
ip tuntap add "$TAP_IF" mode tap

# 3. Agregar TAP al bridge
echo "🔗 Conectando TAP al bridge..."
ip link set "$TAP_IF" master "$BRIDGE_IF"

# 4. Agregar interfaz física al bridge (opcional - solo si quieres que el host también use el bridge)
# Descomenta las siguientes líneas si quieres que el host participe en el bridge:
# echo "📶 Agregando $PHYS_IF al bridge..."
# ip link set "$PHYS_IF" master "$BRIDGE_IF"

# 5. Activar interfaces
echo "⚡ Activando interfaces..."
ip link set "$BRIDGE_IF" up
ip link set "$TAP_IF" up

# 6. Configurar IP en el bridge (necesario para que el host pueda comunicarse con la VM)
echo "🌐 Configurando IP en bridge (192.168.100.1/24)..."
ip addr add 192.168.100.1/24 dev "$BRIDGE_IF" 2>/dev/null || true

# 7. Habilitar forwarding IP en el bridge (necesario para que los paquetes pasen)
echo "📡 Habilitando IP forwarding..."
echo 1 > /proc/sys/net/ipv4/ip_forward 2>/dev/null || sysctl -w net.ipv4.ip_forward=1 2>/dev/null || true

# 8. Deshabilitar filtrado de bridge (permite que los paquetes pasen)
echo "🔓 Deshabilitando filtrado de bridge..."
sysctl -w net.bridge.bridge-nf-call-iptables=0 2>/dev/null || true
sysctl -w net.bridge.bridge-nf-call-ip6tables=0 2>/dev/null || true

echo ""
echo "✅ TAP networking configurado correctamente!"
echo ""
echo "📋 Interfaces creadas:"
echo "   - Bridge: $BRIDGE_IF"
echo "   - TAP: $TAP_IF"
echo ""
echo "🚀 Ahora puedes ejecutar:"
echo "   make run-tap"
echo ""
echo "📝 La VM se auto-configura con IP 192.168.100.2 (si usas 'make run-tap')"
echo ""
echo "🧪 Pruebas de conectividad:"
echo "   Desde el HOST (este terminal):"
echo "     ping 192.168.100.2"
echo ""
echo "   Desde la VM (dentro del kernel):"
echo "     ping 192.168.100.1"
echo ""
echo "🧹 Para limpiar (cuando termines):"
echo "   sudo ip link delete $BRIDGE_IF"
echo "   sudo ip link delete $TAP_IF"

