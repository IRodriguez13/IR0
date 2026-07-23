# IR0 Kernel Internals — Mandoc Index

| Field | Value |
|-------|-------|
| Version | 0.4 |
| IR0 phase | T0–T2 (cross-cutting) |
| Status | stable |
| Man page | (navigation only — use per-subsystem `IR0-<slug>` pages) |

## Purpose

Bilingual, code-faithful kernel documentation under `Documentation/mandocs/`.
Start here: `make man TOPIC=onboarding`. Study subsystems via `man IR0-vfs`, etc.

**Oleada note (0.4):** `IR0-onboarding`; optional boot log → virtio-9p
(`BOOT_LOG_HOSTSHARE` / `make run-bootlog`); honest facade coverage map below.

## Chapter index

| Slug | Man page | Tier | Status | Primary facades (`includes/ir0/`) |
|------|----------|------|--------|-----------------------------------|
| onboarding | IR0-onboarding | T0 | stable | (entry docs — `boot_log_hostshare.h`) |
| boot | IR0-boot | T0 | stable | `boot_log.h`, `arch_port.h` |
| vfs | IR0-vfs | T0 | stable | `path_routed`, VFS facades |
| scheduler | IR0-scheduler | T0 | stable | `sched.h` |
| memory / mm | IR0-memory | T0–T1 | stable | `mm_port.h`, paging/PMM (see also `mm.md`) |
| syscalls | IR0-syscalls | T0–T1 | stable | `copy_user`, open_flags, syscall headers |
| filesystems | IR0-filesystems | T0 | stable | `virtio_9p.h`, blockdev |
| tty | IR0-tty | T1–T2 | stable | `console.h` |
| drivers | IR0-drivers | T0 | stable | `*_backend.h` |
| process | IR0-process | T1 | stable | process / signals facades |
| userspace | IR0-userspace | T1–T2 | stable | exec / ash helpers |
| multi-arch | IR0-multi-arch | T0 | stable | `arch_port.h`, `arm64_board.h` |
| net | IR0-net | T0 | stable | `net.h` |
| interrupts | IR0-interrupts | T0 | stable | irq / arch port |
| ipc | IR0-ipc | T0–T1 | stable | `pipe.h` |
| input | IR0-input | T2 | stable | `input.h`, `input_backend.h` |
| graphics | IR0-graphics | T2 | stable | `fb.h`, `video_backend.h` |
| debug-bins | IR0-debug-bins | T0 | stable | debug shell paths |
| signals | IR0-signals | T1 | stable | `signals.h` |
| security | IR0-security | T0–T1 | stable | cred / sudo paths |

**Not covered yet (honest):** there are ~119 headers under `includes/ir0/`;
mandocs cover subsystem slices, not one page per header. Prefer reading the
facade header + this index over inventing coverage.

`memory.md` is the man chapter; `mm.md` is a COW/depth companion — start with
`IR0-memory`, then `mm.md` in-tree if needed.

Template: `Documentation/mandocs/TEMPLATE.md`. Cursor rule: `ir0-mandocs-initiative`.

## Build

```bash
make sync-mandocs
make man TOPIC=onboarding
make man TOPIC=boot
```

See also: [SETUP.md](../../SETUP.md), [README.md](../../README.md).
