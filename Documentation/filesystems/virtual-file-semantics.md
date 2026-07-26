> **Última verificación:** 2026-07-25
> **Fuente de verdad:** `fs/devfs.c`, `fs/pseudo_fs_registry.c`, `fs/procfs.c`, `ktm/klog.c`, `kernel/syscalls/fs_syscalls.c`

# Virtual file read semantics

## Classes

| Class | open | read | EOF | reopen |
|-------|------|------|-----|--------|
| **Snapshot** | Capture buffer (per open) | Offset + partial | `0` when offset ≥ len | New capture |
| **Bounce snapshot** | No buffer; each read regenerates then slices by offset | Same EOF | Same | Same content race if backend changes mid-stream |
| **Ring non-consumptive** | N/A | Copy from ring without dequeue | `0` at end of current view | May see new lines |
| **Stream / events** | N/A | Block or `EAGAIN`; consume | Usually never (or disconnect) | Continues |

## Snapshot per-open — Implemented (`/dev/net`, `/dev/kmsg`)

```text
open  → devfs_text_snap_capture(device_id) → vfs_file = snap (refs=1)
read  → devfs_text_snap_read(snap, buf, count, offset); advance fd offset
close → devfs_text_snap_release (refs--)
dup   → snap acquire; shared offset (same open file description)
fork  → snap acquire; child inherits offset
```

- No global offset between processes.
- Two `open()` calls get independent buffers and offsets.
- Ops fallback (`dev_net_read` / `dev_kmsg_read`) uses bounce+slice if no snap.

Tests: `ktest_dev_net_contract` (full / 1-byte / EOF / two opens).

## `/proc` via pseudo_fs — Implemented (bounce snapshot)

`pseudo_fs_ops_read` regenerates from offset 0 into a bounce buffer, then copies
`min(count, len - *offset)` and advances `*offset`. EOF when `*offset >= len`.

## Node table (current)

| Path | Class | Notes |
|------|-------|-------|
| `/dev/net` | Snapshot per-open | Text; reopen for fresh iface/ping view |
| `/dev/kmsg` | Snapshot per-open | Captures `klog_read_records` at open |
| `/proc/kmsg` | Bounce + ring non-consumptive | Same ring as klog; **does not dequeue**; distinct from `/dev/kmsg` open snap |
| `/proc/cpuinfo` | Bounce snapshot | EOF after one logical view |
| `/proc/version` | Bounce snapshot | Human line (`IR0 version …`) |
| `/proc/drivers` | Bounce snapshot | |
| `/proc/<pid>/status` | Bounce snapshot | Dynamic pid nodes |
| `/proc/<pid>/stat` | Bounce snapshot | |
| `/dev/events0` | Stream / events | Blocking/nonblock poll; not a text snapshot |
| `/dev/console` stdin | Stream (TTY) | Canon/raw; not EOF-on-file |

## `/dev/kmsg` vs `/proc/kmsg` — Implemented (distinct)

| | `/dev/kmsg` | `/proc/kmsg` |
|--|-------------|--------------|
| Open | Per-open text snap | Pseudo bind; bounce read |
| Consume ring? | No (`klog_read_records` copies) | No |
| Blocking | No (finite snap) | No (finite bounce) |
| Multi-reader | Independent snaps / independent offsets | Independent offsets; shared ring |

BusyBox `dmesg` typically uses `syslog`/`klogctl`, not these paths.

## dup / fork — Implemented

| Op | Snap / pipe / vfs_file |
|----|-------------------------|
| `dup`/`dup2` | Share open file description; shared offset; acquire ref |
| `fork` | Inherit; acquire ref; shared offset until separate open |
| Second `open` | Independent snap + offset |

## Lifecycle

Acquire on open/dup/fork; release on close/exit/destroy. Snap buffer freed when
`refs == 0`.

## Related

- `Documentation/VIRTUAL_FILESYSTEMS.md`
- `Documentation/KLOG.md`
- `Documentation/devfs-io-contract.md`
