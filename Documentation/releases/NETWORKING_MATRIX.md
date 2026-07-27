# Networking capability matrix (lista §9)

> **Última verificación:** 2026-07-27  
> **Fuente de verdad:** código kernel + `IR0-userspace` applets; no “supported” solo por compile.

| capacidad | implementada | probada | limitación | herramienta posible |
|-----------|--------------|---------|------------|---------------------|
| socket(AF_INET, SOCK_DGRAM) | sí | parcial (smokes UDP) | — | nc (off en BusyBox product) |
| socket(AF_INET, SOCK_STREAM) | sí | parcial (TCP smokes) | — | nc / wget (off) |
| bind / connect | sí | parcial | — | — |
| listen / accept | sí | parcial | — | — |
| send/recv / sendto/recvfrom | sí | parcial | — | — |
| poll on sockets | sí | parcial | — | — |
| SIOCGIF* ioctls | sí | parcial | read-mostly | ifconfig (applet off) |
| /sys/class/net | sí | parcial | — | — |
| /proc/netinfo | sí | parcial | TSV raw | — |
| loopback | sí | sí | — | ping lo (applet off) |
| IPv4 | sí | parcial | — | — |
| ICMP | sí | parcial | — | ping (applet off) |
| UDP | sí | parcial | — | — |
| TCP | sí | parcial | — | — |
| DNS | parcial | no product | resolver userspace | nslookup (off) |
| virtio-net | sí | smoke | — | — |
| RTL8139 | sí | smoke | Absent on some QEMU | — |
| AF_NETLINK / ip(8) | **no** | — | no fingir netlink | ip(8) no supported |
| DHCP | bloqueado | — | ABI/userspace | — |
| guest→host / host→guest | parcial | smokes selectos | — | — |
| guest→exterior | no declarado | — | — | — |

**BusyBox product applets (minimal):** `ifconfig` / `ping` / `nc` / `netstat` / `nslookup` / `wget` están deshabilitados hasta smoke real PASS. No habilitar por “compila”.
