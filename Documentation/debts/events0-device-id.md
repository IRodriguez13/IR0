> **Última verificación:** 2026-07-25
> **Fuente de verdad:** `includes/ir0/input.h`, `kernel/input_events.c`, consumers in Doom / smokes

# Debt: `/dev/events0` lacks `device_id`

## Status

**Open — P2.** Separate from PS/2 demux / snapshot / nano series. Do not block
closure of A–D on this.

## Problem

`/dev/events0` publishes a shared ring of `EV_KEY` and `EV_REL` without a
per-device identifier. Keyboard and mouse events are mixed; consumers cannot
filter by device.

## Proposed contract (not implemented)

```c
struct input_event {
    uint64_t timestamp;
    uint16_t type;
    uint16_t code;
    int32_t  value;
    uint32_t device_id; /* NEW */
};
```

## ABI / compatibility analysis

| Topic | Recommendation |
|-------|----------------|
| Break existing Doom/X11/tests? | Yes if layout changes silently. Prefer **versioned** uapi or new ioctl `EVIOCGVERSION` + padded struct. |
| 32/64-bit | Keep fixed-width types; no pointers. Pad to 8-byte alignment (`device_id` + reserved if needed). |
| Endian | LE on x86-64 product; document LE for wire/debug dumps. |
| Stable ids | Assign at register time (`keyboard=1`, `ps2_mouse=2`, …); persist for boot lifetime. |
| Multi-consumer | Keep shared ring; each reader has its own offset (current model). Filter client-side by `device_id`. |
| Migration | Phase 1: append field behind `CONFIG_INPUT_EVENT_V2` or new minor node `/dev/events1`. Phase 2: default V2 when all consumers updated. |

## Recommendation

1. Keep current 16-byte-ish event as V1 for Doom smokes.
2. Add V2 struct + ioctl or second device node.
3. Do not rewrite the ring to per-device queues until V2 consumers exist.

## Out of scope for this series

No code changes until a dedicated issue/oleada.
