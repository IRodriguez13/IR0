#!/bin/bash
# Script para configurar TAP networking para IR0 Kernel
# Uso: sudo ./scripts/setup_tap.sh

set -e

TAP_IF="tap0"
BRIDGE_IF="br0"

echo "🔧 Configurando TAP networking para IR0 Kernel..."

# Verificar si TUN/TAP está disponible
if [ ! -c /dev/net/tun ]; then
    echo "❌ TUN/TAP device no disponible. Cargando módulo..."
    modprobe tun
fi

# Auto-detectar interfaz física activa (la que tiene IP y está UP)
# Prioridad: eth0, enp*, wlan*, wlp*, wlx*
PHYS_IF=""
for iface in $(ip link show | grep -E "^[0-9]+:" | awk '{print $2}' | sed 's/:$//' | grep -vE "^lo$|^br[0-9]+|^virbr|^tap"); do
    # Verificar si la interfaz está UP y tiene IP configurada
    if ip link show "$iface" | grep -q "state UP" && ip -4 addr show "$iface" 2>/dev/null | grep -q "inet"; then
        PHYS_IF="$iface"
        break
    fi
done

# Si no encontramos una con IP, buscar cualquier interfaz UP (excepto loopback y bridges)
if [ -z "$PHYS_IF" ]; then
    for iface in $(ip link show | grep -E "^[0-9]+:" | awk '{print $2}' | sed 's/:$//' | grep -vE "^lo$|^br[0-9]+|^virbr|^tap"); do
        if ip link show "$iface" | grep -q "state UP"; then
            PHYS_IF="$iface"
            break
        fi
    done
fi

# Si todavía no encontramos, mostrar error y sugerir
if [ -z "$PHYS_IF" ]; then
    echo "❌ Error: No se encontró una interfaz de red activa"
    echo "💡 Interfaces disponibles:"
    ip link show | grep -E "^[0-9]+:" | awk '{print $2}' | sed 's/:$//'
    echo ""
    echo "💡 Puedes especificar manualmente editando este script o configurando una interfaz:"
    echo "   sudo ip link set <INTERFAZ> up"
    exit 1
fi

echo "📶 Interfaz física detectada: $PHYS_IF"

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

# CRITICAL: Asegurar que el bridge tenga MAC address estable (evita problemas de aprendizaje)
# El bridge necesita una MAC para responder a ARP requests
if [ -f /sys/class/net/"$BRIDGE_IF"/bridge/bridge_id ]; then
    echo "   ℹ️  Bridge MAC address: $(cat /sys/class/net/"$BRIDGE_IF"/address)"
fi

# 6. Configurar IP en el bridge (necesario para que el host pueda comunicarse con la VM)
echo "🌐 Configurando IP en bridge (192.168.100.1/24)..."
# CRITICAL: Remover IP de otras interfaces que puedan tener conflicto (ej: virbr1)
# Esto evita que el routing de Linux use la interfaz equivocada
for iface in virbr1 virbr0; do
    if ip link show "$iface" &>/dev/null && ip -4 addr show "$iface" 2>/dev/null | grep -q "192.168.100"; then
        echo "   ⚠️  Removiendo conflicto: IP 192.168.100.x de $iface"
        ip addr flush dev "$iface" 2>/dev/null || true
    fi
done
ip addr add 192.168.100.1/24 dev "$BRIDGE_IF" 2>/dev/null || true

# 7. Habilitar forwarding IP (CRITICAL para NAT y routing)
echo "📡 Habilitando IP forwarding..."
echo 1 > /proc/sys/net/ipv4/ip_forward 2>/dev/null || sysctl -w net.ipv4.ip_forward=1 2>/dev/null || true
# Verificar que se habilitó correctamente
if [ "$(cat /proc/sys/net/ipv4/ip_forward 2>/dev/null)" = "1" ]; then
    echo "   ✅ IP forwarding habilitado"
else
    echo "   ⚠️  ADVERTENCIA: IP forwarding no se pudo habilitar - el NAT puede no funcionar"
fi

# 8. Deshabilitar filtrado de bridge (permite que los paquetes pasen)
echo "🔓 Deshabilitando filtrado de bridge..."
# Cargar módulo bridge si no está cargado (necesario para sysctl de bridge)
modprobe bridge 2>/dev/null || true
modprobe br_netfilter 2>/dev/null || true
# Esperar un momento para que los módulos se carguen
sleep 0.5
# CRITICAL: Deshabilitar filtrado de netfilter en bridge (permite que broadcast pase)
# Esto es necesario para que los ARP requests broadcast lleguen al TAP
sysctl -w net.bridge.bridge-nf-call-iptables=0 2>/dev/null || true
sysctl -w net.bridge.bridge-nf-call-ip6tables=0 2>/dev/null || true
sysctl -w net.bridge.bridge-nf-filter-pppoe-tagged=0 2>/dev/null || true
sysctl -w net.bridge.bridge-nf-filter-vlan-tagged=0 2>/dev/null || true

# CRITICAL: Habilitar forwarding de broadcast/multicast en el bridge
echo "📡 Configurando bridge para reenviar broadcast/multicast..."
# Habilitar multicast snooping (permite que multicast pase)
echo 0 > /sys/class/net/"$BRIDGE_IF"/bridge/multicast_snooping 2>/dev/null || true
# Asegurar que el TAP pueda recibir todos los paquetes del bridge
echo 1 > /sys/class/net/"$TAP_IF"/flags 2>/dev/null || true  # IFF_PROMISC equivalent

# 9. Configurar NAT para acceso a Internet desde la VM
echo "🌍 Configurando NAT para acceso a Internet..."
if command -v iptables &>/dev/null; then
    # Obtener la IP de la interfaz física (probablemente tiene una IP DHCP)
    PHYS_IP=$(ip -4 addr show "$PHYS_IF" 2>/dev/null | grep -oP '(?<=inet\s)\d+(\.\d+){3}' | head -1)
    
    if [ -n "$PHYS_IP" ]; then
        # Limpiar reglas anteriores si existen
        iptables -t nat -D POSTROUTING -s 192.168.100.0/24 -o "$PHYS_IF" -j MASQUERADE 2>/dev/null || true
        iptables -D FORWARD -i "$BRIDGE_IF" -o "$PHYS_IF" -j ACCEPT 2>/dev/null || true
        iptables -D FORWARD -i "$PHYS_IF" -o "$BRIDGE_IF" -j ACCEPT 2>/dev/null || true
        
        # CRITICAL: Cargar módulo conntrack si no está cargado (necesario para --state)
        modprobe nf_conntrack 2>/dev/null || true
        
        # Agregar reglas NAT: traducir paquetes del bridge (192.168.100.0/24) a la IP física
        iptables -t nat -A POSTROUTING -s 192.168.100.0/24 -o "$PHYS_IF" -j MASQUERADE
        
        # CRITICAL: Permitir forwarding OUTBOUND (IR0 -> Internet)
        iptables -A FORWARD -i "$BRIDGE_IF" -o "$PHYS_IF" -j ACCEPT
        iptables -A FORWARD -s 192.168.100.0/24 -o "$PHYS_IF" -j ACCEPT
        
        # CRITICAL: Permitir forwarding INBOUND (Internet -> IR0)
        # Esto permite que las respuestas de 8.8.8.8 vuelvan a IR0
        iptables -A FORWARD -i "$PHYS_IF" -o "$BRIDGE_IF" -m state --state RELATED,ESTABLISHED -j ACCEPT
        iptables -A FORWARD -i "$PHYS_IF" -d 192.168.100.0/24 -m state --state RELATED,ESTABLISHED -j ACCEPT
        
        # Fallback: si --state no funciona, permitir todo (menos seguro pero funciona)
        # iptables -A FORWARD -i "$PHYS_IF" -o "$BRIDGE_IF" -j ACCEPT
        
        echo "   ✅ NAT configurado: tráfico de 192.168.100.0/24 → $PHYS_IF ($PHYS_IP)"
    else
        echo "   ⚠️  No se pudo determinar IP de $PHYS_IF, NAT no configurado"
        echo "   💡 Puedes configurar NAT manualmente con:"
        echo "      sudo iptables -t nat -A POSTROUTING -s 192.168.100.0/24 -o $PHYS_IF -j MASQUERADE"
    fi
else
    echo "   ⚠️  iptables no disponible, NAT no configurado"
fi

# Verificación final
echo ""
echo "✅ TAP networking configurado correctamente!"
echo ""
echo "📋 Interfaces creadas:"
echo "   - Bridge: $BRIDGE_IF (IP: 192.168.100.1/24)"
echo "   - TAP: $TAP_IF (conectado a $BRIDGE_IF)"
echo ""
echo "🔍 Verificación de routing:"
echo "   Ruta hacia 192.168.100.2:"
ip route get 192.168.100.2 2>/dev/null || echo "   ⚠️  No se pudo verificar ruta"
echo ""
echo "🚀 Ahora puedes ejecutar:"
echo "   make run-tap"
echo ""
echo "📝 La VM se auto-configura con IP 192.168.100.2 (si usas 'make run-tap')"
echo ""
echo "🧪 Pruebas de conectividad:"
echo "   Desde el HOST (este terminal):"
echo "     ping -c 3 192.168.100.2"
echo "     arp -n 192.168.100.2  # Ver entrada ARP"
echo ""
echo "   Desde la VM (dentro del kernel):"
echo "     ping 192.168.100.1  # Ping al host"
echo "     ping 8.8.8.8        # Ping a Internet (Google DNS)"
echo "     ping google.com     # Ping a dominio (requiere DNS)"
echo ""
echo "⚠️  Si ping falla, verifica:"
echo "   1. Que QEMU esté corriendo (tap0 debe estar UP con LOWER_UP)"
echo "   2. Que no haya otra interfaz con 192.168.100.x (ej: virbr1)"
echo "   3. Que los ARP requests lleguen: ejecuta 'arp -n 192.168.100.2' en host"
echo ""
echo "🧹 Para limpiar (cuando termines):"
echo "   sudo iptables -t nat -D POSTROUTING -s 192.168.100.0/24 -o $PHYS_IF -j MASQUERADE 2>/dev/null || true"
echo "   sudo iptables -D FORWARD -i $BRIDGE_IF -o $PHYS_IF -j ACCEPT 2>/dev/null || true"
echo "   sudo iptables -D FORWARD -i $PHYS_IF -o $BRIDGE_IF -j ACCEPT 2>/dev/null || true"
echo "   sudo ip link delete $BRIDGE_IF"
echo "   sudo ip link delete $TAP_IF"

