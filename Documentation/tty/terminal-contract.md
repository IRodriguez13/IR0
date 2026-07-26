> **Última verificación:** 2026-07-25
> **Fuente de verdad:** `includes/ir0/console.c`, `drivers/video/console.c`, `kernel/syscalls/io_syscalls.c`, `scripts/smoke_desktop_nano_mnt.py`

# Terminal / TTY contract

## Geometry — Implemented

`TIOCGWINSZ` / `TIOCSWINSZ` (`IR0_CONSOLE_TIOCGWINSZ` = `0x5413`) via
`ir0_console_ioctl_winsize*`.

`console_get_geometry()` is always linked (VGA text and framebuffer). It must
**not** live inside `#if CONFIG_ENABLE_VBE` — tiny/matrix builds without VBE
still need winsize for nano and BusyBox.

| Backend | Columns/rows | Cell size |
|---------|--------------|-----------|
| VGA text | 80×25 default | 8×16 early font |
| Framebuffer | Fitted to panel (Terminus product font) | Product cell w/h |

## Termios — Partial

Supported for console stdio: ICANON, ECHO, ISIG, VMIN/VTIME (raw path), basic
control characters (Ctrl-C → SIGINT when ISIG, Ctrl-D EOF in canon, etc.).

Not a full Linux tty driver (no PTY line discipline parity for all ioctls).

## Control input — Implemented

| Sequence | Result |
|----------|--------|
| Ctrl-X (set-1) | Emit `0x18` in raw; no sticky Ctrl after break |
| Ctrl-C | SIGINT when ISIG |
| Arrows | CSI sequences via set-1 E0 prefix |

Probe: `setup/pid1/tty_raw_probe.c` + `scripts/smoke_tty_raw_probe.py`.

## TERM — Partial

Product / nano smoke uses **`TERM=linux`**. Do not advertise `xterm-256color`
unless the CSI/attribute set is implemented.

Supported enough for GNU nano 8.x edit/save/exit on the FB console (smoke
`make smoke-desktop-nano`).

## Application exit — Partial

Apps should restore termios via `tcsetattr`. Kernel does not track per-app
termios stacks. After nano exit, shell expects ECHO+ICANON restored by the
application; verify with `stty -a` / `echo TERMINAL_OK` in smoke.

## Panic visibility — Implemented

`panicex()` clears the active console (FB or VGA) and prints a banner plus
type/location/caller/message on screen **and** serial. Nested panic → emergency
halt without realloc.

## Related tests

| Test | Coverage |
|------|----------|
| `tests/host/test_ps2_set1_*` | Modifiers / Ctrl-X |
| `smoke_tty_raw_probe.py` | QEMU HMP Ctrl-X + arrow |
| `smoke-desktop-nano` | Edit + Ctrl-X save path |
| `ktest_tty_canon_read_immediate` | Canon line ready in buffer |

## Debt: `tty_canon_block_wake`

Boot ktest that schedules a blocked reader from `kmain` was removed from the
boot suite (hang). Correct home: userspace/QEMU smoke or post-scheduler KTM —
see `Documentation/debts/tty-canon-block-wake.md`.

## Related: fitted FB + nano

Scrollback must use `CONSOLE_MAX_WIDTH` (not hard-coded 80) when
`TIOCGWINSZ` reports fitted Terminus geometry. See
`Documentation/debts/nano-exit-ud.md`.
