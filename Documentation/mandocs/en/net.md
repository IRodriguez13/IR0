# IR0 Networking Stack

| Field | Value |
|-------|-------|
| Version | 0.2 |
| IR0 phase | T0 |
| Status | stable |
| Depends on | drivers, interrupts, syscalls |
| Man page | IR0-net (section 7) |
| Primary sources | `net/net.c`, `net/tcp.c`, `net/{arp,ip,icmp,udp,dhcp,dns}.c`, `kernel/sock_stream.c`, `drivers/net/rtl8139.c`, `fs/devfs.c` |

## 1. Overview

IR0 implements an in-kernel IPv4 stack (Ethernet → ARP → IP → ICMP/UDP/TCP wire)
with a single NIC driver (RTL8139). Control and debug still use `/dev/net` and
proc nodes. In addition, **AF_INET `SOCK_STREAM`** is wired through
`kernel/sock_stream.c` onto a minimal **TCP wire** path in `net/tcp.c`
(`tcp_wire_connect` / listen / accept / send / recv / close).

This is **not** a full TCP / Internet stack. Lab paths include limited recovery
(hole rexmit / peer-driven CC smokes — see `smoke-tcp-peer-cc`, `smoke-f8-net`).
There is no production congestion control, no complete out-of-order reassembly,
and no claim of arbitrary Internet reachability. Wire teardown is honest FIN/ACK
+ guest EOF (`recv` returns 0 after peer FIN and drained RX).

## 2. Internal architecture

| Layer | File | Role |
|-------|------|------|
| Core | `net/net.c` | Device/protocol lists, `net_send`, `net_receive`, `init_net_stack` |
| L2 resolve | `net/arp.c` | ARP cache (64 entries, 5 min timeout) |
| L3 | `net/ip.c` | IPv4 TX/RX, routing table (16 routes), TX fragmentation |
| ICMP | `net/icmp.c` | Echo request/reply, ping pending list |
| UDP | `net/udp.c` | Port handlers for DHCP/DNS |
| TCP wire | `net/tcp.c` | SYN/ACK handshake, payload, FIN/ACK teardown, inbound listeners |
| Stream sockets | `kernel/sock_stream.c` | AF_INET SOCK_STREAM → `tcp_wire_*` |
| DHCP/DNS | `net/dhcp.c`, `net/dns.c` | Client-only DHCP; DNS A-record resolver |
| NIC | `drivers/net/rtl8139.c` | Probe, TX/RX ring, IRQ hook |
| Control | `fs/devfs.c` | `/dev/net` device_id 8, ioctl 0x3001–0x3004 |

Facade: `includes/ir0/net.h`, `net/tcp.h`. When `CONFIG_ENABLE_NETWORKING=0`,
`kernel/net_compat.c` provides weak stubs.

## 3. Data flow

**Boot (networking enabled):**

```text
  init_all_drivers → init_net_stack()
       → rtl8139_init() (net_stack_probe_drivers)
       → arp_init → ip_init → icmp_init → udp_init → dns_init
       → ip_init sets static IPv4 (QEMU user: 10.0.2.15/24, gw 10.0.2.2)
  sti → DHCP not auto; needs write "dhcp" to /dev/net or post-IRQ devfs hook
```

**TX (ping example):**

```text
  write /dev/net "ping host"
       → dev_net_write → icmp_send_echo_request
       → ip_send → arp_resolve → net_send (Ethernet hdr)
       → rtl8139_send
```

**AF_INET stream (client → host):**

```text
  socket/connect/send/recv/close
       → sock_stream_* → tcp_wire_connect / send / recv / close
       → IP/Ethernet → RTL8139
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
  RTL8139 IRQ or net_stack_poll()
       → net_receive → demux EtherType
       → ARP handler OR ip_receive → ICMP/UDP/TCP wire handlers
```

**Idle:** `kernel_idle_poll()` → `net_stack_poll()` per registered device.

ASCII:

```text
  app ──► sock_stream / /dev/net ──► TCP/ICMP/DNS ──► IP ──► ARP ──► net_send
                                                                    │
  RTL8139 IRQ ◄─────────────────────────────────────────────────────┘
       │
       └──► net_receive ──► protocol handlers
```

## 4. Responsibilities

- Drivers register `net_device` with MTU (default 1500) and send/poll/IRQ ops.
- Protocols register handlers; `net_receive` demultiplexes.
- `/dev/net` parses text commands (ping, dhcp, ifconfig) and ioctl API.
- `sock_stream` owns listen/accept/connect state; `tcp_wire_*` owns segments.
- debug_bins (`ping`, `ifconfig`, `route`, `netstat`) use syscalls only.

## 5. Subsystem boundaries

- `net/` uses `includes/ir0/*`; no direct `#include <drivers/...>` from portable trees.
- TCP wire is a **lab** path for smokes and AF_INET STREAM — not BSD sockets parity.
  AF_UNIX streams share `sock_stream` (see `IR0-ipc`).
- Device/protocol lists assume registration before interrupts enabled (no locks).

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|---------------|
| Drivers | RTL8139 bootstrap stage NETWORK |
| Interrupts | `net_stack_handle_irq` from ISR |
| Syscalls | socket/bind/listen/accept/connect/send/recv → sock_stream |
| devfs | `/dev/net` ops and poll |
| procfs | `/proc/netinfo`, `/proc/net/dev` |
| debug_bins | `cmd_ping.c`, `cmd_ifconfig.c`, etc. |

## 7. Visual maps

```text
  ┌──────────────┐     ┌──────────┐     ┌─────────┐
  │ sock_stream  │────►│ TCP wire │────►│ RTL8139 │
  │ /dev/net     │     │ IP/ICMP  │     │  NIC    │
  │ debug_bins   │     │ ARP/UDP  │     └─────────┘
  └──────────────┘     └──────────┘
         │                  ▲
         └──── read snapshot/proc ────┘
```

## 8. Important invariants

1. `net_send` rejects `len > dev->mtu`; frame sanity cap 2000 bytes.
2. No inbound IP fragment reassembly (`IP_RX_REASSEMBLY_SUPPORTED 0`).
3. Multicast non-ARP frames dropped early in `net_receive`.
4. Ping identity uses `(pid & 0xFFFF)` for ICMP echo matching.
5. Only RTL8139 probed by default; E1000 define exists but not wired.
6. `tcp_wire_recv`: drained + `peer_fin` → 0; drained + !fin → `-EAGAIN`.
7. `tcp_wire_close` emits FIN|ACK; peer ACK tracked via `tcp_wire_peer_ack`.

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
