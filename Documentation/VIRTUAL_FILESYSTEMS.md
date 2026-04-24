# IR0 Virtual Filesystems

This document focuses on pseudo-filesystems exposed through VFS.

## `/proc`

`procfs` exposes runtime kernel and process data.

### Common Endpoints

- `/proc/meminfo`
- `/proc/uptime`
- `/proc/version`
- `/proc/filesystems`
- `/proc/mounts`
- `/proc/drivers`
- `/proc/interrupts`
- `/proc/blockdevices`
- `/proc/partitions`
- `/proc/[pid]/status`
- `/proc/[pid]/cmdline`

### Notes

- Data is generated at read time.
- Numeric formatting was hardened for 64-bit values.
- Per-process pseudo-fd context tracking avoids cross-process collisions.

## `/dev`

`devfs` exposes kernel device entry points.

### Common Nodes

- `/dev/null`, `/dev/zero`
- `/dev/console`, `/dev/tty`
- `/dev/kmsg`
- `/dev/disk`
- `/dev/net`
- `/dev/audio`
- `/dev/mouse`

### Notes

- Access uses standard syscall I/O from user-style binaries.
- Device registration is routed through driver/bootstrap infrastructure.

## `/sys`

`sysfs` exposes kernel/system data in a structured filesystem namespace.

### Notes

- Error handling paths use consistent negative errno returns.
- Console and backend exposure route through facade-backed interfaces.

## In-Memory Pseudo Backends

- `tmpfs`: writable memory-backed tree with uid/gid and umask-aware create.
- `procfs`, `devfs`, `sysfs`: dynamic pseudo filesystems.

## Strengths

- Strong observability at runtime without external debug tooling.
- Consistent user-facing access model through open/read/write/stat patterns.

## Weak Points

- Some endpoints remain intentionally minimal and need richer semantics.
- Coverage of edge-case parsing/format compatibility still depends on runtime tests.

