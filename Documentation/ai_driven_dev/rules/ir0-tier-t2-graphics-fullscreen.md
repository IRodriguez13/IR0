<!-- IR0 AI dev rule: ir0-tier-t2-graphics-fullscreen -->
<!-- alwaysApply: false -->
<!-- description: T2 (~50%) — Fullscreen graphics client fb0 evdev mmap (Doom-class) -->

# T2 — Cliente Gráfico Fullscreen (Doom-class)

## Goal

Run a static fullscreen client using `/dev/fb0` + `/dev/events0` + `mmap` without a window manager.

## Mandatory web research

- Linux uapi: `linux/fb.h` (`FBIOGET_*`, `fb_var_screeninfo`), `linux/input.h` (`input_event` layout).
- OSDev: VBE linear framebuffer, double-buffering constraints.
- Doom/Linux port expectations: resolution, bpp, event codes (research port docs or nxDoom/musl doom notes).

Fetch or search **before** changing ioctl layouts or struct sizes — ABI breaks silently.

## Verify in tree

- `sys_mmap` special case for fb0 (`device_id == 15`) — generalize carefully.
- `dev_events0_read` — `struct input_event` size/endianness vs uapi.
- VBE path: `drivers/video/vbe.c`, Multiboot fb tags.

## Multi-agent split

1. **Agent A**: Research + document minimal ioctl/mmap contract in DECOUPLING (English).
2. **Agent B**: Harden fb0 + events0 + mmap (facades, no driver leaks in syscalls).
3. **Agent C**: Host or ktest struct-size tests; optional QEMU checklist for one known static binary.

## Out of scope (defer to T3)

Window management, overlapping windows, GPU acceleration, Wayland/X11.

## Done criteria

- Documented fb0 + events0 ABI matches Linux uapi subset used by target client.
- Client runs fullscreen in QEMU with PS/2 input (USB HID is T1/T2 parallel track via USB scaffold).
