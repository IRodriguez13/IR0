# Pila de red de IR0

| Campo | Valor |
|-------|-------|
| Versión | 0.3 |
| Fase IR0 | T0 |
| Estado | stable |
| Depende de | drivers, interrupts, syscalls |
| Página man | IR0-net (sección 7) |
| Fuentes principales | `includes/ir0/netdevice.h`, `net/dev.c`, `drivers/net/net_drivers.c`, `net/net.c`, `net/tcp.c`, `net/{arp,ip,icmp,udp,dhcp,dns}.c`, `net/sock_stream.c`, `drivers/net/rtl8139.c`, `fs/devfs.c` |

## 1. Visión general

IR0 implementa una pila IPv4 en el kernel (Ethernet → ARP → IP → ICMP/UDP/TCP
wire) sobre drivers de NIC que se conectan a una única API de clase de
dispositivo, `<ir0/netdevice.h>` (RTL8139 y virtio-net en x86-64, BCM2711
GENET en arm64). Control y depuración siguen usando `/dev/net` y nodos proc. Además, **AF_INET `SOCK_STREAM`** está cableado vía
`net/sock_stream.c` a un camino mínimo **TCP wire** en `net/tcp.c`
(`tcp_wire_connect` / listen / accept / send / recv / close).

No es una pila TCP / Internet completa. Hay caminos de laboratorio con
recuperación limitada (rexmit de huecos / CC peer — ver `smoke-tcp-peer-cc`,
`smoke-f8-net`). No hay control de congestión de producción, ni reensamblado
fuera de orden completo, ni alcance Internet arbitrario. El teardown es FIN/ACK
honesto + EOF en guest (`recv` devuelve 0 tras FIN del peer y RX drenado).

## 2. Arquitectura interna

| Capa | Archivo | Rol |
|------|---------|-----|
| Clase de dispositivo | `net/dev.c` | Lista de dispositivos, registro, estado de enlace, cambios de MTU/MAC, hooks de poll e IRQ |
| Núcleo | `net/net.c` | Lista de protocolos, `net_send`, `netif_rx`, `init_net_stack` |
| Resolución L2 | `net/arp.c` | Caché ARP (64 entradas, timeout 5 min) |
| L3 | `net/ip.c` | TX/RX IPv4, tabla de rutas (16 rutas), fragmentación TX |
| ICMP | `net/icmp.c` | Echo request/reply, lista pendiente de ping |
| UDP | `net/udp.c` | Handlers de puerto para DHCP/DNS |
| TCP wire | `net/tcp.c` | Handshake SYN/ACK, payload, teardown FIN/ACK, listeners inbound |
| Stream sockets | `net/sock_stream.c` | AF_INET SOCK_STREAM → `tcp_wire_*` |
| DHCP/DNS | `net/dhcp.c`, `net/dns.c` | Cliente DHCP únicamente; resolvedor DNS registro A |
| Drivers de NIC | `drivers/net/{rtl8139,virtio_net,bcmgenet}.c` | Probe del hardware, anillos TX/RX, `net_device_ops` |
| Tabla de drivers | `drivers/net/net_drivers.c` | `net_stack_probe_drivers()` recorre las entradas `struct net_driver` configuradas |
| Control | `fs/devfs.c` | `/dev/net` device_id 8, ioctl 0x3001–0x3004 |

Fachadas: `includes/ir0/netdevice.h` (la API de clase de NIC, abajo) e
`includes/ir0/net.h` (hooks de arranque, IRQ y poll, la configuración IPv4 vía `ip_get_config()` / `ip_set_config()` y los helpers de
`/dev/net`) y `net/tcp.h`. Los demás headers `net/*.h`, incluido
`net/net_internal.h` (registro de protocolos y `net_send`), son privados de
`net/`. Con `CONFIG_ENABLE_NETWORKING=0`, `net/net_compat.c` proporciona stubs
débiles para los hooks que se llaman desde fuera del stack.

### API de clase de NIC

Cada NIC es un `struct net_device` (nombre, `dev_addr`, `flags`, `mtu`, `irq`,
`state` de enlace, un `struct net_device_stats`) con un
`const struct net_device_ops *netdev_ops`. Los protocolos, los ioctl de sockets
y los pseudo filesystems usan sólo esta API; ningún driver exporta funciones.

| Op | Obligatoria | Si es NULL |
|----|-------------|------------|
| `ndo_start_xmit(dev, frame, len)` | sí | `register_netdev()` devuelve `-EINVAL` |
| `ndo_open(dev)` / `ndo_stop(dev)` | no | `dev_open()` / `dev_close()` sólo cambian los flags |
| `ndo_set_mac_address(dev, addr)` | no | `dev_set_mac_address()` devuelve `-EOPNOTSUPP`; `eth_mac_addr()` sirve a las placas que no filtran por dirección |
| `ndo_poll_controller(dev)` | no | `net_stack_poll()` saltea la placa |

| Llamada | Dueño | Rol |
|---------|-------|-----|
| `alloc_etherdev(priv)`, `netdev_priv()`, `free_netdev()` | driver | valores Ethernet (MTU 1500, `IFF_BROADCAST`, sin IRQ) más un área del driver |
| `register_netdev()`, `unregister_netdev()` | driver | publicar o retirar el dispositivo |
| `netif_rx(dev, frame, len)` | driver | entregar al stack un frame Ethernet recibido |
| `netif_carrier_on/off()` | driver | informar el enlace |
| `dev->stats` | driver + núcleo | paquetes, bytes, errores, desbordes de FIFO; el núcleo cuenta descartes y multicast |
| `dev_open()`, `dev_close()`, `dev_change_flags()` | núcleo | `IFF_UP` e `IFF_RUNNING` (arriba con portadora) son estado del núcleo |
| `dev_set_mtu()`, `dev_set_mac_address()` | núcleo | `SIOCSIFMTU` (68..65535), `SIOCSIFHWADDR` |
| `first_net_device()`, `for_each_netdev()`, `dev_get_by_name()` | llamadores | búsquedas, el dispositivo más nuevo primero |

Un driver con interrupción la pide con `irq_request()` y guarda la línea en
`dev->irq`; uno sin interrupción deja `irq = -1` e implementa
`ndo_poll_controller`. Ninguna NIC anuncia offloads, así que todavía no hay
bits `NETIF_F_*`: el primer driver con offload de checksum, scatter-gather o
VLAN agrega el bit junto con la rama del protocolo que lo usa.

## 3. Flujo de datos

**Arranque (red habilitada):**

```text
  init_all_drivers → init_net_stack()
       → net_stack_probe_drivers(): el probe de cada net_driver
         → alloc_etherdev + register_netdev (+ irq_request)
       → dev_open() en cada dispositivo registrado
       → arp_init → ip_init → icmp_init → udp_init → dns_init
       → ip_init fija IPv4 estática (QEMU user: 10.0.2.15/24, gw 10.0.2.2)
  sti → DHCP no automático; requiere write "dhcp" a /dev/net o hook devfs post-IRQ
```

**TX (ejemplo ping):**

```text
  write /dev/net "ping host"
       → dev_net_write → icmp_send_echo_request
       → ip_send → arp_resolve → net_send (Ethernet hdr)
       → dev->netdev_ops->ndo_start_xmit
```

**AF_INET stream (cliente → host):**

```text
  socket/connect/send/recv/close
       → sock_stream_* → tcp_wire_connect / send / recv / close
       → IP/Ethernet → ndo_start_xmit
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
  IRQ de NIC (irq_dispatch) o ndo_poll_controller (net_stack_poll)
       → netif_rx → demux EtherType
       → handler ARP O ip_receive → handlers ICMP/UDP/TCP wire
```

**Idle:** la tarea idle (`sched/idle.c`) llama a `net_stack_poll()`, que corre el `ndo_poll_controller` de cada dispositivo.

ASCII:

```text
  app ──► sock_stream / /dev/net ──► TCP/ICMP/DNS ──► IP ──► ARP ──► net_send
                                                                    │
  IRQ / poll NIC ◄──────────────────────────────── ndo_start_xmit ◄┘
       │
       └──► netif_rx ──► protocol handlers
```

## 4. Responsabilidades

- Los drivers llenan un `net_device` y sus `net_device_ops`, informan la
  portadora y cuentan en `dev->stats`; el núcleo es dueño de la lista de
  dispositivos, de `IFF_UP`/`IFF_RUNNING` y de las respuestas a las ops ausentes.
- Los protocolos registran handlers; `netif_rx` demultiplexa.
- `/dev/net` parsea comandos de texto (ping, dhcp, ifconfig) e ioctl.
- `sock_stream` posee estado listen/accept/connect; `tcp_wire_*` los segmentos.
- Userspace BusyBox (`ping`, `ifconfig`, `route`, `netstat`) usa solo syscalls.

### Agregar una NIC

1. Escribir `drivers/net/<placa>.c` con un probe estático que encuentre el
   hardware, haga `alloc_etherdev()`, complete `name`, `dev_addr` y
   `netdev_ops`, llame a `netif_carrier_on/off()`, `register_netdev()` e
   `irq_request()` para su línea (o implemente `ndo_poll_controller`).
2. Implementar `ndo_start_xmit`; dejar en NULL toda op que la placa no tenga.
   Contar paquetes, bytes y errores en `dev->stats`; entregar los frames
   recibidos a `netif_rx()`.
3. Exportar un único `const struct net_driver <placa>_driver` (declarado en
   `drivers/net/net_drivers.h`) y listarlo en `drivers/net/net_drivers.c` bajo
   su símbolo Kconfig; agregar el objeto y el símbolo al `Makefile` y a
   `setup/Kconfig`.
4. Nada fuera de `drivers/net/` cambia: ni header público, ni llamadas desde
   `net/`, `fs/` o `kernel/`.

## 5. Límites del subsistema

- `net/` usa `includes/ir0/*`; sin `#include <drivers/...>` directo desde árboles portables.
- TCP wire es un camino de **laboratorio** para smokes y AF_INET STREAM — no
  paridad BSD. AF_UNIX stream comparte `sock_stream` (ver `IR0-ipc`).
- Listas device/protocol asumen registro antes de habilitar IRQs (sin locks).

## 6. Relaciones con otros subsistemas

| Vecino | Interacción |
|--------|-------------|
| Drivers | `net_stack_probe_drivers()` (etapa NETWORK del bootstrap) recorre la tabla de drivers |
| Interrupts | Las NIC piden su línea con `irq_request()`; el ISR x86 llama a `net_stack_handle_irq()`, que las despacha y hace poll de los dispositivos sin IRQ |
| Syscalls | socket/bind/listen/accept/connect/send/recv → sock_stream |
| devfs | ops y poll de `/dev/net` |
| procfs | `/proc/netinfo`, `/proc/net/dev` |
| ISD | applets BusyBox / smokes de red |

## 7. Mapas visuales

```text
  ┌──────────────┐     ┌──────────┐     ┌─────────┐
  │ sock_stream  │────►│ TCP wire │────►│ netdev  │
  │ /dev/net     │     │ IP/ICMP  │     │ ops/NIC │
  │ userspace    │     │ ARP/UDP  │     └─────────┘
  └──────────────┘     └──────────┘
         │                  ▲
         └──── lectura snapshot/proc ────┘
```

## 8. Invariantes importantes

1. `net_send` rechaza `len > dev->mtu`; tope de sanity 2000 bytes.
2. Sin reensamblado de fragmentos IP inbound (`IP_RX_REASSEMBLY_SUPPORTED 0`).
3. Frames multicast no-ARP se descartan temprano en `netif_rx`.
4. Identidad de ping usa `(pid & 0xFFFF)` para matching ICMP echo.
5. x86-64 hace probe de virtio-net y después RTL8139; arm64 de GENET con
   `CONFIG_DRV_NIC_BCMGENET=y`; E1000 definido pero no cableado.
6. `tcp_wire_recv`: drenado + `peer_fin` → 0; drenado + !fin → `-EAGAIN`.
7. `tcp_wire_close` emite FIN|ACK; ACK del peer vía `tcp_wire_peer_ack`.
8. `IFF_UP` e `IFF_RUNNING` son estado del núcleo: `SIOCSIFFLAGS` abre o
   cierra el dispositivo y nunca fija `IFF_RUNNING`, que significa arriba con
   portadora.

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
