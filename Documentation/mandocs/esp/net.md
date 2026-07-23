# Pila de red de IR0

| Campo | Valor |
|-------|-------|
| Versión | 0.2 |
| Fase IR0 | T0 |
| Estado | stable |
| Depende de | drivers, interrupts, syscalls |
| Página man | IR0-net (sección 7) |
| Fuentes principales | `net/net.c`, `net/tcp.c`, `net/{arp,ip,icmp,udp,dhcp,dns}.c`, `kernel/sock_stream.c`, `drivers/net/rtl8139.c`, `fs/devfs.c` |

## 1. Visión general

IR0 implementa una pila IPv4 en el kernel (Ethernet → ARP → IP → ICMP/UDP/TCP
wire) con un único driver de NIC (RTL8139). Control y depuración siguen usando
`/dev/net` y nodos proc. Además, **AF_INET `SOCK_STREAM`** está cableado vía
`kernel/sock_stream.c` a un camino mínimo **TCP wire** en `net/tcp.c`
(`tcp_wire_connect` / listen / accept / send / recv / close).

No es una pila TCP / Internet completa. Hay caminos de laboratorio con
recuperación limitada (rexmit de huecos / CC peer — ver `smoke-tcp-peer-cc`,
`smoke-f8-net`). No hay control de congestión de producción, ni reensamblado
fuera de orden completo, ni alcance Internet arbitrario. El teardown es FIN/ACK
honesto + EOF en guest (`recv` devuelve 0 tras FIN del peer y RX drenado).

## 2. Arquitectura interna

| Capa | Archivo | Rol |
|------|---------|-----|
| Núcleo | `net/net.c` | Listas device/protocol, `net_send`, `net_receive`, `init_net_stack` |
| Resolución L2 | `net/arp.c` | Caché ARP (64 entradas, timeout 5 min) |
| L3 | `net/ip.c` | TX/RX IPv4, tabla de rutas (16 rutas), fragmentación TX |
| ICMP | `net/icmp.c` | Echo request/reply, lista pendiente de ping |
| UDP | `net/udp.c` | Handlers de puerto para DHCP/DNS |
| TCP wire | `net/tcp.c` | Handshake SYN/ACK, payload, teardown FIN/ACK, listeners inbound |
| Stream sockets | `kernel/sock_stream.c` | AF_INET SOCK_STREAM → `tcp_wire_*` |
| DHCP/DNS | `net/dhcp.c`, `net/dns.c` | Cliente DHCP únicamente; resolvedor DNS registro A |
| NIC | `drivers/net/rtl8139.c` | Probe, anillo TX/RX, hook IRQ |
| Control | `fs/devfs.c` | `/dev/net` device_id 8, ioctl 0x3001–0x3004 |

Fachada: `includes/ir0/net.h`, `net/tcp.h`. Con `CONFIG_ENABLE_NETWORKING=0`,
`kernel/net_compat.c` proporciona stubs débiles.

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
       → ip_send → arp_resolve → net_send (Ethernet hdr)
       → rtl8139_send
```

**AF_INET stream (cliente → host):**

```text
  socket/connect/send/recv/close
       → sock_stream_* → tcp_wire_connect / send / recv / close
       → IP/Ethernet → RTL8139
  Peer FIN → inbound.peer_fin → tcp_wire_recv retorna 0 (EOF)
  RX vacío sin FIN → -EAGAIN
```

**AF_INET stream (listen):**

```text
  bind/listen → tcp_wire_listen_register(port)
  accept → tcp_wire_accept_take (SYN → SYN-ACK → inbound taken)
  recv payload → tcp_wire_recv
  peer shutdown(WR) → FIN → ACK → recv EOF (0)
```

**RX:**

```text
  RTL8139 IRQ o net_stack_poll()
       → net_receive → demux EtherType
       → handler ARP O ip_receive → handlers ICMP/UDP/TCP wire
```

**Idle:** `kernel_idle_poll()` → `net_stack_poll()` por dispositivo registrado.

ASCII:

```text
  app ──► sock_stream / /dev/net ──► TCP/ICMP/DNS ──► IP ──► ARP ──► net_send
                                                                    │
  RTL8139 IRQ ◄─────────────────────────────────────────────────────┘
       │
       └──► net_receive ──► protocol handlers
```

## 4. Responsabilidades

- Los drivers registran `net_device` con MTU (1500) y ops send/poll/IRQ.
- Los protocolos registran handlers; `net_receive` demultiplexa.
- `/dev/net` parsea comandos de texto (ping, dhcp, ifconfig) e ioctl.
- `sock_stream` posee estado listen/accept/connect; `tcp_wire_*` los segmentos.
- debug_bins (`ping`, `ifconfig`, `route`, `netstat`) usan solo syscalls.

## 5. Límites del subsistema

- `net/` usa `includes/ir0/*`; sin `#include <drivers/...>` directo desde árboles portables.
- TCP wire es un camino de **laboratorio** para smokes y AF_INET STREAM — no
  paridad BSD. AF_UNIX stream comparte `sock_stream` (ver `IR0-ipc`).
- Listas device/protocol asumen registro antes de habilitar IRQs (sin locks).

## 6. Relaciones con otros subsistemas

| Vecino | Interacción |
|--------|-------------|
| Drivers | RTL8139 bootstrap etapa NETWORK |
| Interrupts | `net_stack_handle_irq` desde ISR |
| Syscalls | socket/bind/listen/accept/connect/send/recv → sock_stream |
| devfs | ops y poll de `/dev/net` |
| procfs | `/proc/netinfo`, `/proc/net/dev` |
| debug_bins | `cmd_ping.c`, `cmd_ifconfig.c`, etc. |

## 7. Mapas visuales

```text
  ┌──────────────┐     ┌──────────┐     ┌─────────┐
  │ sock_stream  │────►│ TCP wire │────►│ RTL8139 │
  │ /dev/net     │     │ IP/ICMP  │     │  NIC    │
  │ debug_bins   │     │ ARP/UDP  │     └─────────┘
  └──────────────┘     └──────────┘
         │                  ▲
         └──── lectura snapshot/proc ────┘
```

## 8. Invariantes importantes

1. `net_send` rechaza `len > dev->mtu`; tope de sanity 2000 bytes.
2. Sin reensamblado de fragmentos IP inbound (`IP_RX_REASSEMBLY_SUPPORTED 0`).
3. Frames multicast no-ARP se descartan temprano en `net_receive`.
4. Identidad de ping usa `(pid & 0xFFFF)` para matching ICMP echo.
5. Solo RTL8139 en probe por defecto; E1000 definido pero no cableado.
6. `tcp_wire_recv`: drenado + `peer_fin` → 0; drenado + !fin → `-EAGAIN`.
7. `tcp_wire_close` emite FIN|ACK; ACK del peer vía `tcp_wire_peer_ack`.

## 9. Consejos de depuración

- Tags: `NET:`, `LOG_*("IP")`, `RTL8139`, `[BOOT]` desenmascarado IRQ red.
- Smoke listen: `F8_TCP_LISTEN_BOUND_OK`, `ACCEPT_OK`, `EOF_OK`, `F8_TCP_LISTEN_OK`.
- Smokes cliente: `smoke-tcp-wire` / `smoke-tcp-guest` (evidencia hostfwd; batería `smoke-f8-net`).
- NIC reach: `NIC_PING_REPLY_OK` obligatorio antes de `F8_NIC_REACH_OK`.
- Helpers host: `scripts/tcp_wire_host_connector.py`, `scripts/tcp_wire_host_listener.py`.
- `ndev` / lectura de `/dev/net` para snapshot.
- Compilar con `CONFIG_ENABLE_NETWORKING=y`; QEMU `-device rtl8139`.

## 10. Roadmap futuro

- **Retransmisión / RTO / ventana / congestión peer-driven más profunda** — hay
  probes de lab; selftests no son ship gate de “TCP Internet hecho”.
- **IPv6** — descartado en L2.
- **API BSD sockets completa** (options, OOB, MSG_*) — AF_UNIX + AF_INET STREAM parcial.
- **DHCP automático al arranque** — IP estática hasta comando `dhcp` explícito.
- E1000: **BLOQUEADO** — sin driver MVP (`CONFIG_DRV_NIC_E1000=n`); no reclamar.
- virtio-net: smoke L3 (`smoke-nic-reach-virtio`); no es el probe por defecto en todos los paths.
- VirtualBox: **no** canónico; usar QEMU + 9p.

Legacy: mencionado en `Documentation/DRIVERS.md`, `Documentation/mandocs/en/drivers.md`.
