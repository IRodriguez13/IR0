# Pila de red de IR0

| Campo | Valor |
|-------|-------|
| Versión | 0.1 |
| Fase IR0 | T0 |
| Estado | stable |
| Depende de | drivers, interrupts, syscalls |
| Página man | IR0-net (sección 7) |
| Fuentes principales | `net/net.c`, `net/{arp,ip,icmp,udp,dhcp,dns}.c`, `drivers/net/rtl8139.c`, `fs/devfs.c`, `kernel/syscalls.c` |

## 1. Visión general

IR0 implementa una pila IPv4 en el kernel (Ethernet → ARP → IP → ICMP/UDP) con un
único driver de NIC (RTL8139). Userspace y las herramientas de depuración acceden
a la pila mediante `/dev/net` y nodos proc — **no** mediante syscalls socket POSIX
(éstas devuelven `ENOSYS`).

Este capítulo documenta el comportamiento implementado hoy: cliente DHCP/DNS,
comandos de texto en `/dev/net`, y polling en idle. La capa socket no existe.

## 2. Arquitectura interna

| Capa | Archivo | Rol |
|------|---------|-----|
| Núcleo | `net/net.c` | Listas device/protocol, `net_send`, `net_receive`, `init_net_stack` |
| Resolución L2 | `net/arp.c` | Caché ARP (64 entradas, timeout 5 min) |
| L3 | `net/ip.c` | TX/RX IPv4, tabla de rutas (16 rutas), fragmentación TX |
| ICMP | `net/icmp.c` | Echo request/reply, lista pendiente de ping |
| UDP | `net/udp.c` | Handlers de puerto para DHCP/DNS |
| DHCP/DNS | `net/dhcp.c`, `net/dns.c` | Cliente DHCP únicamente; resolvedor DNS registro A |
| NIC | `drivers/net/rtl8139.c` | Probe, anillo TX/RX, hook IRQ |
| Control | `fs/devfs.c` | `/dev/net` device_id 8, ioctl 0x3001–0x3004 |

Fachada: `includes/ir0/net.h`. Con `CONFIG_ENABLE_NETWORKING=0`, `kernel/net_compat.c`
proporciona stubs débiles.

## 3. Flujo de datos

**Arranque (red habilitada):**

```text
  init_all_drivers → init_net_stack()
       → rtl8139_init() (net_stack_probe_drivers)
       → arp_init → ip_init → icmp_init → udp_init → dns_init
       → ip_init fija IPv4 estática (QEMU user: 10.0.2.15/24, gw 10.0.2.2)
  sti → DHCP no automático; requiere write "dhcp" a /dev/net o hook devfs post-IRQ
```

**TX (ejemplo ping):**

```text
  write /dev/net "ping host"
       → dev_net_write → icmp_send_echo_request
       → ip_send → arp_resolve → net_send (cabecera Ethernet)
       → rtl8139_send
```

**RX:**

```text
  IRQ RTL8139 o net_stack_poll()
       → net_receive → demux EtherType
       → handler ARP O ip_receive → handlers ICMP/UDP
```

**Idle:** `kernel_idle_poll()` → `net_stack_poll()` por cada dispositivo registrado.

Mapa ASCII:

```text
  app/debug_bin ──► /dev/net ──► ICMP/DNS ──► IP ──► ARP ──► net_send
                                                      │
  IRQ RTL8139 ◄───────────────────────────────────────┘
       │
       └──► net_receive ──► handlers de protocolo
```

## 4. Responsabilidades

- Los drivers registran `net_device` con MTU (predeterminado 1500) y ops send/poll/IRQ.
- Los protocolos registran handlers; `net_receive` desmultiplexa.
- `/dev/net` parsea comandos de texto (ping, dhcp, ifconfig) y la API ioctl.
- Los debug_bins (`ping`, `ifconfig`, `route`, `netstat`) usan solo syscalls.

## 5. Límites del subsistema

- `net/` usa `includes/ir0/*`; sin `#include <drivers/...>` directo desde árboles portables.
- No hay capa socket en el kernel; la tabla de syscalls enlaza todos los `__NR_socket*` a `sys_nosys`.
- Las listas device/protocol asumen registro antes de habilitar interrupciones (sin locks).

## 6. Relación con otros subsistemas

| Vecino | Interacción |
|--------|-------------|
| Drivers | RTL8139 etapa bootstrap NETWORK |
| Interrupts | `net_stack_handle_irq` desde ISR |
| devfs | ops y poll de `/dev/net` |
| procfs | `/proc/netinfo`, `/proc/net/dev` |
| debug_bins | `cmd_ping.c`, `cmd_ifconfig.c`, etc. |

## 7. Mapas visuales

```text
  ┌─────────────┐     ┌──────────┐     ┌─────────┐
  │ /dev/net    │────►│ IP/ICMP  │────►│ RTL8139 │
  │ debug_bins  │     │ ARP/UDP  │     │  NIC    │
  └─────────────┘     └──────────┘     └─────────┘
         │                  ▲
         └──── lectura snapshot/proc ────┘
```

## 8. Invariantes importantes

1. `net_send` rechaza `len > dev->mtu`; tope de sanidad de trama 2000 bytes.
2. Sin reensamblado de fragmentos IP entrantes (`IP_RX_REASSEMBLY_SUPPORTED 0`).
3. Tramas multicast no-ARP descartadas pronto en `net_receive`.
4. La identidad de ping usa `(pid & 0xFFFF)` para emparejar echo ICMP.
5. Solo RTL8139 en probe por defecto; define E1000 existe pero no está cableado.

## 9. Consejos de depuración

- Tags: `NET:`, `LOG_*("IP")`, `RTL8139`, `[BOOT]` desenmascarado IRQ red.
- `ndev` / lectura de `/dev/net` para snapshot.
- `ping` vía debug shell o write a `/dev/net`.
- Compilar con `CONFIG_ENABLE_NETWORKING=y`; QEMU `-device rtl8139`.

## 10. Roadmap futuro

- **TCP** — no implementado (`IPPROTO_TCP` sin uso).
- **IPv6** — descartado en L2.
- **Syscalls socket** — todos `ENOSYS`.
- **DHCP automático al arranque** — IP estática hasta comando `dhcp` explícito.
- Driver E1000 fuera del path de probe.

Legacy: mencionado en `Documentation/DRIVERS.md`, `Documentation/mandocs/en/drivers.md`.
