> **Última verificación:** 2026-07-25
> **Fuente de verdad:** `interrupt/arch/keyboard.c`, `drivers/IO/ps2_mouse.c`, `includes/ir0/ps2_mouse_pkt.c`, `includes/ir0/ps2_set1.c`, `includes/ir0/input_backend.c`

# PS/2 keyboard/mouse routing (i8042)

## Status legend

| Tag | Meaning |
|-----|---------|
| **Implemented** | Behavior present in tree and covered by host and/or QEMU evidence |
| **Partial** | Works for the product path; gaps documented |
| **Planned** | Not implemented; debt only |

## Overview

IR0 uses IBM PC AT/2 scan code **set 1** for the keyboard and the classic
3/4-byte PS/2 mouse packet format. Keyboard and mouse share the i8042 data
port (`0x60`); demultiplexing uses status bit **AUXDATA (0x20)** on `0x64`.

## i8042 demux — Implemented

```text
IRQ1 / IRQ12 / idle poll
  → read status (0x64)
  → if OUTPUT_FULL: read data (0x60)
  → if AUXDATA: input_mouse_feed_byte() → ps2_mouse_pkt_feed()
  → else: keyboard_feed_scancode() → ps2_set1_feed() → TTY / EV_KEY
```

- Classification happens **before** any keyboard decoder.
- IRQ1 and IRQ12 both call `keyboard_poll_ps2()` so neither steals the other’s bytes.
- Mouse init enables `PS2_CFG_INT2` after AUX port bring-up.

Debug (off by default): `CONFIG_DEBUG_PS2` → `[PS2] status=… source=keyboard|mouse`.

## Keyboard modifiers — Implemented

Portable state in `struct keyboard_modifiers` (`includes/ir0/ps2_set1.h`):

- Left/Right Ctrl, Shift, Alt (independent bits)
- Caps / Num / Scroll lock toggles
- `ctrl = left_ctrl || right_ctrl` (derived, never a sticky single bool)

Make/break set-1 rules apply to modifiers (break clears the matching side).
Autorepeat make codes do not invent a key-up.

Host tests: `tests/host/test_ps2_set1_modifiers.c`, `tests/host/test_ps2_mouse_pkt.c`.

## Mouse packets — Implemented

`includes/ir0/ps2_mouse_pkt.c`:

- Resync: byte 0 must have bit 3 set (`ALWAYS_1`)
- Accumulate 3 or 4 bytes; publish only complete packets
- Incomplete packets are never published

Mouse path must never mutate keyboard modifier state.

## `/dev/events0` — Partial

Keyboard and mouse both push into one `struct input_event` ring
(`time`, `type`, `code`, `value`) with **no `device_id`**.

| Aspect | Status |
|--------|--------|
| EV_KEY from keyboard | Implemented |
| EV_REL / button EV_KEY from mouse | Implemented |
| Per-device filter / device_id | **Planned** — see `Documentation/debts/events0-device-id.md` |

## Invariants

1. Mouse motion/buttons never emit TTY bytes.
2. Mouse bytes never update `keyboard_modifiers`.
3. AUX bytes are never fed to `ps2_set1_feed`.
4. Modifier release requires the matching break code (or equivalent).

## Related docs

- Mandoc overview: `Documentation/mandocs/en/input.md`
- TTY contract: `Documentation/tty/terminal-contract.md`
