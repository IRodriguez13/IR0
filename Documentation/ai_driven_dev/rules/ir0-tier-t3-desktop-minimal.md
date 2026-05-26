<!-- IR0 AI dev rule: ir0-tier-t3-desktop-minimal -->
<!-- alwaysApply: false -->
<!-- description: T3 (~15–20%) — Escritorio minimalista WM+panel planning only; kernel prep boundaries -->

# T3 — Escritorio Minimalista (WM + panel)

## Scope boundary

**Kernel repo**: sockets/TCP prep, USB HID, proc/sys stability, TLS threads — **not** a compositor or panel implementation here.

Desktop lives in **separate userspace repos** (WM, terminal, panel) once T1+T2 gates pass.

## Mandatory web research (planning tasks)

When user asks for T3 planning, research with web:

- Minimal stacks: **fbdev + direct input** vs **Wayland** (wlroots) vs **Xorg fbdev** — pick one for IR0 constraints.
- Reference projects: **dwm/sowm** (X11), **labwc** (Wayland), **fbterm** (fb only) — compare syscall needs.
- TCP + `socket()` requirements for any “desktop with network applets”.

## Kernel prerequisites before T3 userspace

| Prerequisite | Tier |
|--------------|------|
| musl + init + fork/exec stable | T1 |
| fb0 + evdev + mmap stable | T2 |
| `socket`/`connect`/`poll` + IPv4 TCP | New P1 (not in tree — verify with grep) |
| USB HID or PS/2 mouse path | T2/T1 |

## Multi-agent planning mode

Use **Plan mode** + 3 explore agents:

1. Syscall/socket gap vs chosen WM stack.
2. Graphics/input path fit for IR0 VBE.
3. Build/integration (cross-compile rootfs, minix disk layout).

Output: phased plan with **no kernel WM code** unless user explicitly requests in-kernel fb compositor (discouraged).

## Honesty rule

State ~15–20% for T3 until T1 boot and T2 client proof exist; do not claim “desktop ready” after kernel-only changes.
