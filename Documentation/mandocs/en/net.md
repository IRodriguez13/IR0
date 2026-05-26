# IR0 Networking Stack

| Field | Value |
|-------|-------|
| Version | 0.1 |
| IR0 phase | T0 |
| Status | stable |
| Depends on | drivers, interrupts, syscalls |
| Man page | IR0-net (section 7) |
| Primary sources | `net/net.c`, `net/{arp,ip,icmp,udp,dhcp,dns}.c`, `drivers/net/rtl8139.c`, `fs/devfs.c`, `kernel/syscalls.c` |

## 1. Overview

IR0 implements an in-kernel IPv4 stack (Ethernet → ARP → IP → ICMP/UDP) with a
single NIC driver (RTL8139). Userspace and debug tools reach the stack through
`/dev/net` and proc nodes — **not** through POSIX socket syscalls (those return
`ENOSYS`).

## 2. Internal architecture

| Layer | File | Role |
|-------|------|------|
| Core | `net/net.c` | Device/protocol lists, `net_send`, `net_receive`, `init_net_stack` |
| L2 resolve | `net/arp.c` | ARP cache (64 entries, 5 min timeout) |
| L3 | `net/ip.c` | IPv4 TX/RX, routing table (16 routes), TX fragmentation |
| ICMP | `net/icmp.c` | Echo request/reply, ping pending list |
| UDP | `net/udp.c` | Port handlers for DHCP/DNS |
| DHCP/DNS | `net/dhcp.c`, `net/dns.c` | Client-only DHCP; DNS A-record resolver |
| NIC | `drivers/net/rtl8139.c` | Probe, TX/RX ring, IRQ hook |
| Control | `fs/devfs.c` | `/dev/net` device_id 8, ioctl 0x3001–0x3004 |

Facade: `includes/ir0/net.h`. When `CONFIG_ENABLE_NETWORKING=0`, `kernel/net_compat.c` provides weak stubs.

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

**RX:**

```text
  RTL8139 IRQ or net_stack_poll()
       → net_receive → demux EtherType
       → ARP handler OR ip_receive → ICMP/UDP handlers
```

**Idle:** `kernel_idle_poll()` → `net_stack_poll()` per registered device.

ASCII:

```text
  app/debug_bin ──► /dev/net ──► ICMP/DNS ──► IP ──► ARP ──► net_send
                                                      │
  RTL8139 IRQ ◄──────────────────────────────────────┘
       │
       └──► net_receive ──► protocol handlers
```

## 4. Responsibilities

- Drivers register `net_device` with MTU (default 1500) and send/poll/IRQ ops.
- Protocols register handlers; `net_receive` demultiplexes.
- `/dev/net` parses text commands (ping, dhcp, ifconfig) and ioctl API.
- debug_bins (`ping`, `ifconfig`, `route`, `netstat`) use syscalls only.

## 5. Subsystem boundaries

- `net/` uses `includes/ir0/*`; no direct `#include <drivers/...>` from portable trees.
- No socket layer in kernel; syscalls table wires all `__NR_socket*` to `sys_nosys`.
- Device/protocol lists assume registration before interrupts enabled (no locks).

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|---------------|
| Drivers | RTL8139 bootstrap stage NETWORK |
| Interrupts | `net_stack_handle_irq` from ISR |
| devfs | `/dev/net` ops and poll |
| procfs | `/proc/netinfo`, `/proc/net/dev` |
| debug_bins | `cmd_ping.c`, `cmd_ifconfig.c`, etc. |

## 7. Visual maps

```text
  ┌─────────────┐     ┌──────────┐     ┌─────────┐
  │ /dev/net    │────►│ IP/ICMP  │────►│ RTL8139 │
  │ debug_bins  │     │ ARP/UDP  │     │  NIC    │
  └─────────────┘     └──────────┘     └─────────┘
         │                  ▲
         └──── read snapshot/proc ────┘
```

## 8. Important invariants

1. `net_send` rejects `len > dev->mtu`; frame sanity cap 2000 bytes.
2. No inbound IP fragment reassembly (`IP_RX_REASSEMBLY_SUPPORTED 0`).
3. Multicast non-ARP frames dropped early in `net_receive`.
4. Ping identity uses `(pid & 0xFFFF)` for ICMP echo matching.
5. Only RTL8139 probed by default; E1000 define exists but not wired.

## 9. Debugging tips

- Tags: `NET:`, `LOG_*("IP")`, `RTL8139`, `[BOOT]` net IRQ unmask.
- `ndev` / read `/dev/net` for snapshot.
- `ping` via debug shell or write to `/dev/net`.
- Build with `CONFIG_ENABLE_NETWORKING=y`; QEMU `-device rtl8139`.

## 10. Future roadmap

- **TCP** — not implemented (`IPPROTO_TCP` unused).
- **IPv6** — dropped at L2.
- **Socket syscalls** — all `ENOSYS`.
- **Auto DHCP at boot** — static IP until explicit `dhcp` command.
- E1000 driver not in probe path.

Legacy: mentioned in `Documentation/DRIVERS.md`, `Documentation/mandocs/en/drivers.md`.
