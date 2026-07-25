# IR0 — Structured klog event core

> **Last verified:** 2026-07-24
> **Source of truth:** `includes/ir0/klog_event.h`, `ktm/klog.c`, `ktm/include/klog.h`,
> `kernel/cmdline.c`, `arch/common/boot_log.c`,
> `arch/common/arch_interface.c` (`arch_early_clock_*`), `fs/procfs.c` (`proc_kmsg_read`),
> `fs/devfs.c` (`dev_kmsg_*`), `includes/ir0/boot_log_hostshare.c`, `kernel/main.c`

Human kernel logging is an **event core**, not a free-form string dump. KTM is an
optional observer of the same stream; `CONFIG_KTM=n` keeps klog, serial, console,
`/proc/kmsg`, and `/dev/kmsg` alive (`make smoke-klog-ktm-off`).

## Presentation profiles

| Profile | Default threshold | Typical content |
|---------|-------------------|-----------------|
| `quiet` | NOTICE | Banner, warnings, failures, milestones |
| `normal` | INFO | Phase milestones, driver one-liners, summary |
| `debug` | DEBUG | Register/init ceremony |
| `trace` | TRACE | `OPEN_ABI`, `VFS_STAT`, internal events |

- **Kconfig:** `LOG_PROFILE_{QUIET,NORMAL,DEBUG,TRACE}` (product default: normal).
- **Runtime:** Multiboot cmdline `ir0.loglevel=quiet|normal|debug|trace` and
  `ir0.trace=open_abi,vfs` (see `kernel/cmdline.c`, `arch/x86-64/grub.cfg`).
- **`klog_smoke()`** always emits (autokill tags) regardless of profile.
- Legacy `LOG_*` macros share the same global threshold as `klog_*`.

## Record layout

```c
struct klog_record {
    uint64_t sequence;      /* always assigned, never invented retrospectively */
    uint64_t timestamp_ns;  /* 0 unless clock_state == MONOTONIC */
    uint64_t raw_ticks;     /* early arch counter when RAW */
    uint32_t event_id;      /* klog_event_id_t */
    uint16_t subsystem;
    uint8_t  severity;      /* klog_level_t */
    uint8_t  clock_state;   /* UNAVAILABLE|RAW|CALIBRATED|MONOTONIC */
    uint8_t  boot_phase;    /* EARLY_ARCH … READY */
    uint8_t  cpu_id;
    char     component[24];
    char     message[96];
};
```

API: `klog_event()`, `klog_set_boot_phase()`, `klog_read_records()`,
`klog_promote_normal_ring()`. Legacy `klog_info` / `klog_notice_fmt` emit as
`KLOG_EVENT_GENERIC`.

### Event IDs (today)

| ID | Meaning |
|----|---------|
| `GENERIC` | Default levelled emits |
| `BOOT_BANNER` / `BOOT_INFO` | Portable boot contract (`ir0_boot_*`) |
| `DRIVER_PROBE_RESULT` | One line per registered driver |
| `DRIVER_SUMMARY` | Aggregated ready/absent/deferred/unsupported/failed |
| `USERSPACE_HANDOFF` | `exec /sbin/init` |
| `FIRST_BAREMETAL_BOOT` | First successful bare-metal boot (rootfs sentinel) |

### Boot phases

`EARLY_ARCH` → `MEMORY` → `PLATFORM` → `DRIVERS` → `STORAGE` → `ROOTFS` →
`TIME` → `INTERRUPTS` → `READY` → `USERSPACE`.

### Early clock

`arch_early_clock_available()` / `read()` / `quality()` (x86 `rdtsc`, ARM64
`cntpct_el0`). Quality starts as `RAW` — duration is **not** invented; the
renderer prints `[    ?.???]` until the monotonic clock is online.

### Ring

Static early ring of 256 records; `klog_promote_normal_ring()` after heap → 1024.

## Renderer / sinks

Human line:

```text
[#000042] [DRIVERS] [    ?.???] [NOTICE] [DRIVERS] summary ready=…
[#000151] [USERSPACE] [1.418] [INFO] [SMOKE] GETTY_READY
```

| Sink | Behavior |
|------|----------|
| Serial | Always (after `klog_boot_hold(0)`) |
| Console (VGA/FB) | Rate-limited: NOTICE+ and BOOT/INIT/CLOCK/DRIVERS/PLATFORM/HYPERVISOR |
| `/proc/kmsg` | `klog_read_records()` |
| `/dev/kmsg` | Same read backend; writes become `klog_info("USER", …)` |
| Hostshare | `make run-bootlog` / `smoke-boot-log-hostshare` — reuses `disk.img` when present; virtio-9p for `ir0-boot.log` only |
| KTM protocol | Optional mirror via `klog_set_protocol_mirror` when `CONFIG_KTM=y` |

Boot completion (product): `kernel core initialization complete` → (optional)
`KTM validation complete` → `system ready for userspace` before `kexecve`.

Component tags use UPPER_SNAKE (`DRIVER`, `PS2_MOUSE`, `PC_SPEAKER`, `ADLIB`).
QEMU host warnings live on host stderr (`*.qemu-stderr`), not the guest serial stream.

## Product path (no dbgshell)

Desktop defconfigs: `CONFIG_KERNEL_DEBUG_SHELL=n`, `CONFIG_DEBUG_BINS=n`.  
Daily `make run` / `run-console` / `run-bootlog` → `kernel-x64-userspace.iso` +
runit/getty/ash. Legacy ring-0 shell is opt-in (`CONFIG_DEBUG_BINS` /
`CONFIG_KERNEL_DEBUG_SHELL`) for isolated contracts only.

Exploration: `cat /proc/kmsg` or `dmesg` from ash — not the in-kernel shell.

## Gates

| Target | Expect |
|--------|--------|
| `make smoke-runit-boot` | Sequence lines + `GETTY_READY` / `DRIVER_SUMMARY_OK` |
| `make smoke-klog-ktm-off` | Boot with `CONFIG_KTM=n`; `[#…] [EARLY_ARCH]` present |
| `make smoke-boot-log-hostshare` | Host `ir0-boot.log` contains `IR0 kernel` + records |
| `make pre-submit` | Build + arch-guard + host tests |

## Related docs

- [`KTM.md`](KTM.md) — KTM protocol vs klog layers  
- [`mandocs/en/boot.md`](mandocs/en/boot.md) — boot pipeline + phases  
- [`VIRTUAL_FILESYSTEMS.md`](VIRTUAL_FILESYSTEMS.md) — `/proc/kmsg`, `/dev/kmsg`  
- [`mandocs/en/debug-bins.md`](mandocs/en/debug-bins.md) — legacy harness status  
