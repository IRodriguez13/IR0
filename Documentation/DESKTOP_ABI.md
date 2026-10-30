# Desktop ABI (IR0 side)

> **Last verified:** 2026-09-18  
> **Sibling truth:** [ISD `Documentation/DESKTOP_ABI.md`](https://github.com/IRodriguez13/ISD/blob/master/Documentation/DESKTOP_ABI.md)

## Golden rule

Programs we want to run define the kernel surface. IR0 grows Linux-compatible
ABI (`/dev/fb0`, evdev, AF_UNIX, SysV shm, `/proc/loadavg`, …). ISD packages
upstream clients **without** IR0-specific source patches.

## Host / guest inspectors

| Tool | Where | Role |
|------|-------|------|
| `python3 scripts/userspace_manager.py` | IR0 host | ISD version, package origins, desktop package set (not a make target) |
| `ir0-status version` | guest | ISD release stamp + live `uname` |
| `python3 scripts/kernel_manager.py` | IR0 host | Kernel ISO catalog (`#N` is machine-local; not a make target) |

`ISD_VERSION` and kernel build `#N` must never be conflated.
