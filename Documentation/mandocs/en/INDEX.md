# IR0 Kernel Internals — Mandoc Index

| Field | Value |
|-------|-------|
| Version | 0.3 |
| IR0 phase | T0–T2 (cross-cutting) |
| Status | stable |
| Man page | (navigation only — use per-subsystem `IR0-<slug>` pages) |

## Purpose

Bilingual, code-faithful kernel documentation under `Documentation/mandocs/`.
Study IR0 from inside the running system via `man IR0-vfs`, `man IR0-net`, etc.

**Oleada note (0.3):** documented AF_INET TCP wire + FIN/EOF teardown; ARM F7h–F7j
process/TTBR freestanding; virtio-9p symlink; priority scheduler default;
`arch_first_context_switch` facade.

## Chapter index

| Slug | Man page | Tier | Status |
|------|----------|------|--------|
| vfs | IR0-vfs | T0 | stable |
| boot | IR0-boot | T0 | stable |
| scheduler | IR0-scheduler | T0 | stable |
| memory | IR0-memory | T0–T1 | stable |
| syscalls | IR0-syscalls | T0–T1 | stable |
| filesystems | IR0-filesystems | T0 | stable |
| tty | IR0-tty | T1–T2 | stable |
| drivers | IR0-drivers | T0 | stable |
| process | IR0-process | T1 | stable |
| userspace | IR0-userspace | T1–T2 | stable |
| multi-arch | IR0-multi-arch | T0 | stable |
| net | IR0-net | T0 | stable |
| interrupts | IR0-interrupts | T0 | stable |
| ipc | IR0-ipc | T0–T1 | stable |
| input | IR0-input | T2 | stable |
| graphics | IR0-graphics | T2 | stable |
| debug-bins | IR0-debug-bins | T0 | stable |
| signals | IR0-signals | T1 | stable |
| security | IR0-security | T0–T1 | stable |

Template: `Documentation/mandocs/TEMPLATE.md`. Cursor rule: `ir0-mandocs-initiative`.

## Build

```bash
make mandocs-en
man IR0-net
python3 scripts/build_mandocs.py --lang en --mandoc-only --no-install
```

Diagrams are **inline ASCII in each chapter** (mandoc-safe).

See also: [SETUP.md](../../SETUP.md), [Documentation/README.md](../README.md).
