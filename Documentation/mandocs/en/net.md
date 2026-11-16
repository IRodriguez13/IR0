# IR0 Networking Stack

| Field | Value |
|-------|-------|
| Version | 0.3 |
| IR0 phase | T0 |
| Status | stable |
| Depends on | drivers, interrupts, syscalls |
| Man page | IR0-net (section 7) |
| Primary sources | `includes/ir0/netdevice.h`, `net/dev.c`, `drivers/net/net_drivers.c`, `net/net.c`, `net/tcp.c`, `net/{arp,ip,icmp,udp,dhcp,dns}.c`, `net/sock_stream.c`, `drivers/net/rtl8139.c`, `fs/devfs.c` |

## 1. Overview

IR0 implements an in-kernel IPv4 stack (Ethernet → ARP → IP → ICMP/UDP/TCP wire)
on NIC drivers that plug into one device class API, `<ir0/netdevice.h>`
(RTL8139 and virtio-net on x86-64, BCM2711 GENET on arm64). Control and debug
still use `/dev/net` and proc nodes. In addition, **AF_INET `SOCK_STREAM`** is wired through
`net/sock_stream.c` onto a minimal **TCP wire** path in `net/tcp.c`
(`tcp_wire_connect` / listen / accept / send / recv / close).

This is **not** a full TCP / Internet stack. Lab paths include limited recovery
(hole rexmit / peer-driven CC smokes — see `smoke-tcp-peer-cc`, `smoke-f8-net`).
There is no production congestion control, no complete out-of-order reassembly,
and no claim of arbitrary Internet reachability. Wire teardown is honest FIN/ACK
+ guest EOF (`recv` returns 0 after peer FIN and drained RX).

## 2. Internal architecture

| Layer | File | Role |
|-------|------|------|
| Device class | `net/dev.c` | Device list, registration, link state, MTU/MAC changes, poll and IRQ hooks |
| Core | `net/net.c` | Protocol list, `net_send`, `netif_rx`, `init_net_stack` |
| L2 resolve | `net/arp.c` | ARP cache (64 entries, 5 min timeout) |
| L3 | `net/ip.c` | IPv4 TX/RX, routing table (16 routes), TX fragmentation |
| ICMP | `net/icmp.c` | Echo request/reply, ping pending list |
| UDP | `net/udp.c` | Port handlers for DHCP/DNS |
| TCP wire | `net/tcp.c` | SYN/ACK handshake, payload, FIN/ACK teardown, inbound listeners |
| Stream sockets | `net/sock_stream.c` | AF_INET SOCK_STREAM → `tcp_wire_*` |
| DHCP/DNS | `net/dhcp.c`, `net/dns.c` | Client-only DHCP; DNS A-record resolver |
| NIC drivers | `drivers/net/{rtl8139,virtio_net,bcmgenet}.c` | Hardware probe, TX/RX rings, `net_device_ops` |
| Driver table | `drivers/net/net_drivers.c` | `net_stack_probe_drivers()` walks the configured `struct net_driver` entries |
| Control | `fs/devfs.c` | `/dev/net` device_id 8, ioctl 0x3001–0x3004 |

Facades: `includes/ir0/netdevice.h` (the NIC class API below) and
`includes/ir0/net.h` (boot, IRQ and poll hooks, the IPv4 configuration through `ip_get_config()` / `ip_set_config()`, and the
`/dev/net` helpers) and `net/tcp.h`. The other `net/*.h` headers, including
`net/net_internal.h` (protocol registration and `net_send`), are private to
`net/`. When `CONFIG_ENABLE_NETWORKING=0`, `net/net_compat.c` provides weak
stubs for the hooks called from outside the stack.

### NIC class API

Every NIC is one `struct net_device` (name, `dev_addr`, `flags`, `mtu`, `irq`,
link `state`, one `struct net_device_stats`) with a
`const struct net_device_ops *netdev_ops`. Protocols, socket ioctls and the
pseudo filesystems use only this API; no driver exports a function.

| Op | Required | When NULL |
|----|----------|-----------|
| `ndo_start_xmit(dev, frame, len)` | yes | `register_netdev()` returns `-EINVAL` |
| `ndo_open(dev)` / `ndo_stop(dev)` | no | `dev_open()` / `dev_close()` only change the flags |
| `ndo_set_mac_address(dev, addr)` | no | `dev_set_mac_address()` returns `-EOPNOTSUPP`; `eth_mac_addr()` serves cards that filter nothing by address |
| `ndo_poll_controller(dev)` | no | `net_stack_poll()` skips the card |

| Call | Owner | Role |
|------|-------|------|
| `alloc_etherdev(priv)`, `netdev_priv()`, `free_netdev()` | driver | Ethernet defaults (MTU 1500, `IFF_BROADCAST`, no IRQ) plus a driver area |
| `register_netdev()`, `unregister_netdev()` | driver | publish or withdraw the device |
| `netif_rx(dev, frame, len)` | driver | hand a received Ethernet frame to the stack |
| `netif_carrier_on/off()` | driver | report the link |
| `dev->stats` | driver + core | packets, bytes, errors, FIFO overruns; the core counts drops and multicast |
| `dev_open()`, `dev_close()`, `dev_change_flags()` | core | `IFF_UP` and `IFF_RUNNING` (up with a carrier) are core state |
| `dev_set_mtu()`, `dev_set_mac_address()` | core | `SIOCSIFMTU` (68..65535), `SIOCSIFHWADDR` |
| `first_net_device()`, `for_each_netdev()`, `dev_get_by_name()` | callers | lookups, newest device first |

A driver with an interrupt requests it with `irq_request()` and stores the
line in `dev->irq`; one without leaves `irq = -1` and implements
`ndo_poll_controller`. No NIC advertises an offload, so there are no
`NETIF_F_*` feature bits yet: the first driver with checksum offload,
scatter-gather or VLAN adds the bit together with the protocol branch that
uses it.

## 3. Data flow

**Boot (networking enabled):**

```text
  init_all_drivers → init_net_stack()
       → net_stack_probe_drivers(): each net_driver probe
         → alloc_etherdev + register_netdev (+ irq_request)
       → dev_open() on every registered device
       → arp_init → ip_init → icmp_init → udp_init → dns_init
       → ip_init sets static IPv4 (QEMU user: 10.0.2.15/24, gw 10.0.2.2)
  sti → DHCP not auto; needs write "dhcp" to /dev/net or post-IRQ devfs hook
```

**TX (ping example):**

```text
  write /dev/net "ping host"
       → dev_net_write → icmp_send_echo_request
       → ip_send → arp_resolve → net_send (Ethernet hdr)
       → dev->netdev_ops->ndo_start_xmit
```

**AF_INET stream (client → host):**

```text
  socket/connect/send/recv/close
       → sock_stream_* → tcp_wire_connect / send / recv / close
       → IP/Ethernet → ndo_start_xmit
  Peer FIN → inbound.peer_fin → tcp_wire_recv returns 0 (EOF)
  Empty RX without FIN → -EAGAIN
```

**AF_INET stream (listen):**

```text
  bind/listen → tcp_wire_listen_register(port)
  accept → tcp_wire_accept_take (SYN → SYN-ACK → taken inbound)
  recv payload → tcp_wire_recv
  peer shutdown(WR) → FIN → ACK → recv EOF (0)
```

**RX:**

```text
  NIC IRQ (irq_dispatch) or ndo_poll_controller (net_stack_poll)
       → netif_rx → demux EtherType
       → ARP handler OR ip_receive → ICMP/UDP/TCP wire handlers
```

**Idle:** the idle task (`sched/idle.c`) calls `net_stack_poll()`, which runs every device's `ndo_poll_controller`. ARP and DHCP waits poll the NIC and sleep `clock_sleep()` between polls.

ASCII:

```text
  app ──► sock_stream / /dev/net ──► TCP/ICMP/DNS ──► IP ──► ARP ──► net_send
                                                                    │
  NIC IRQ / poll ◄──────────────────────────────── ndo_start_xmit ◄┘
       │
       └──► netif_rx ──► protocol handlers
```

## 4. Responsibilities

- Drivers fill a `net_device` and its `net_device_ops`, report the carrier and
  count into `dev->stats`; the core owns the device list, `IFF_UP`/`IFF_RUNNING`
  and the answers for missing ops.
- Protocols register handlers; `netif_rx` demultiplexes.
- `/dev/net` parses text commands (ping, dhcp, ifconfig) and ioctl API.
- `sock_stream` owns listen/accept/connect state; `tcp_wire_*` owns segments.
- userspace (BusyBox) (`ping`, `ifconfig`, `route`, `netstat`) use syscalls only.

### Adding a NIC

1. Write `drivers/net/<card>.c` with a static probe that finds the hardware,
   `alloc_etherdev()`s the device, fills `name`, `dev_addr` and
   `netdev_ops`, calls `netif_carrier_on/off()`, `register_netdev()`, and
   `irq_request()` for its line (or implements `ndo_poll_controller`).
2. Implement `ndo_start_xmit`; leave every op the card lacks NULL. Count
   packets, bytes and errors in `dev->stats`; hand received frames to
   `netif_rx()`.
3. Export one `const struct net_driver <card>_driver` (declared in
   `drivers/net/net_drivers.h`) and list it in `drivers/net/net_drivers.c`
   under its Kconfig symbol; add the object and the symbol to the `Makefile`
   and `setup/Kconfig`.
4. Nothing outside `drivers/net/` changes: no public header, no call from
   `net/`, `fs/` or `kernel/`.

## 5. Subsystem boundaries

- `net/` uses `includes/ir0/*`; no direct `#include <drivers/...>` from portable trees.
- TCP wire is a **lab** path for smokes and AF_INET STREAM — not BSD sockets parity.
  AF_UNIX streams share `sock_stream` (see `IR0-ipc`).
- Device/protocol lists assume registration before interrupts enabled (no locks).

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|---------------|
| Drivers | `net_stack_probe_drivers()` (bootstrap stage NETWORK) walks the driver table |
| Interrupts | NICs request lines with `irq_request()`; the x86 ISR calls `net_stack_handle_irq()`, which dispatches them and polls IRQ-less devices |
| Syscalls | socket/bind/listen/accept/connect/send/recv → sock_stream |
| devfs | `/dev/net` ops and poll |
| procfs | `/proc/netinfo`, `/proc/net/dev` |
| userspace (BusyBox) | `cmd_ping.c`, `cmd_ifconfig.c`, etc. |

## 7. Visual maps

```text
  ┌──────────────┐     ┌──────────┐     ┌─────────┐
  │ sock_stream  │────►│ TCP wire │────►│ netdev  │
  │ /dev/net     │     │ IP/ICMP  │     │ ops/NIC │
  │ userspace (BusyBox)   │     │ ARP/UDP  │     └─────────┘
  └──────────────┘     └──────────┘
         │                  ▲
         └──── read snapshot/proc ────┘
```

## 8. Important invariants

1. `net_send` rejects `len > dev->mtu`; frame sanity cap 2000 bytes.
2. No inbound IP fragment reassembly (`IP_RX_REASSEMBLY_SUPPORTED 0`).
3. Multicast non-ARP frames dropped early in `netif_rx`.
4. Ping identity uses `(pid & 0xFFFF)` for ICMP echo matching.
5. x86-64 probes virtio-net, then RTL8139; arm64 probes GENET when
   `CONFIG_DRV_NIC_BCMGENET=y`; E1000 define exists but not wired.
6. `tcp_wire_recv`: drained + `peer_fin` → 0; drained + !fin → `-EAGAIN`.
7. `tcp_wire_close` emits FIN|ACK; peer ACK tracked via `tcp_wire_peer_ack`.
8. `IFF_UP` and `IFF_RUNNING` are core state: `SIOCSIFFLAGS` opens or closes
   the device and never sets `IFF_RUNNING`, which means up with a carrier.

## 9. Debugging tips

- Tags: `NET:`, `LOG_*("IP")`, `RTL8139`, `[BOOT]` net IRQ unmask.
- TCP listen smoke: `F8_TCP_LISTEN_BOUND_OK`, `ACCEPT_OK`, `EOF_OK`, `F8_TCP_LISTEN_OK`.
- TCP client smokes: `smoke-tcp-wire` / `smoke-tcp-guest` (peer hostfwd evidence; battery `smoke-f8-net`).
- NIC reach: `NIC_PING_REPLY_OK` required before `F8_NIC_REACH_OK`.
- Host helpers: `scripts/tcp_wire_host_connector.py`, `scripts/tcp_wire_host_listener.py`.
- `ndev` / read `/dev/net` for snapshot.
- Build with `CONFIG_ENABLE_NETWORKING=y`; QEMU `-device rtl8139`.

## 10. Future roadmap

- **Deeper peer-driven retransmit / RTO / window / congestion** — lab probes exist;
  selftest stubs are not ship gates for “Internet TCP done”.
- **IPv6** — dropped at L2.
- **Full BSD socket API** (options, OOB, MSG_*) — partial AF_UNIX + AF_INET STREAM.
- **Auto DHCP at boot** — static IP until explicit `dhcp` command.
- E1000: **BLOCKED** — no driver MVP (`CONFIG_DRV_NIC_E1000=n`); do not claim.
- virtio-net: L3 reach smoke exists (`smoke-nic-reach-virtio`); not default probe for all paths.
- VirtualBox networking/share: **not** a supported product path; use QEMU + 9p.

Legacy: mentioned in `Documentation/DRIVERS.md`, `Documentation/mandocs/en/drivers.md`.
