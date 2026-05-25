# FASE58E — Interactive BusyBox ash on `/dev/console` (QEMU GTK)

**Status:** Done (2026-05-25)  
**Tier:** T1 (minimal POSIX userspace) — vertical slice  
**Depends on:** FASE58A console handoff, devfs fd binding, irinit stdio attach

## Summary

IR0 boots **irinit** → **BusyBox ash** on the framebuffer console. The user sees
the `#` prompt, **typed characters echo on screen**, and **Enter submits a line**
to the shell (e.g. `ls`, `pwd`, `echo hi`).

This closes the last blocker for a real interactive shell on QEMU GTK without
patching BusyBox for userspace echo.

## What the user sees

1. Banner: `BusyBox v1.36.1 … built-in shell (ash)`
2. White `#` prompt (stderr, color `0x0F`)
3. Characters appear as they are typed (kernel TTY echo, ICANON + ECHO)
4. Enter runs the command; stdout/stderr render on the same FB console

Commands such as `ps` may print `not found` if they are not linked into the
fase50 minimal BusyBox config — that is expected and unrelated to console I/O.

## Root causes fixed

Two separate bugs were involved:

### 1. No keyboard polling while ash blocked on `read(0)`

When ash calls `read(STDIN_FILENO, …)` and no input is available, the process
blocks in `tty_sleep_for_input()`. The round-robin scheduler has **no dedicated
idle thread** yet; when every task is blocked, `sched_schedule_next()` returns
without switching to another runnable task.

Previously, `kernel_idle_poll()` (which calls `keyboard_poll_ps2()`) only ran
from `kmain`'s fallback loop — but that loop is **unreachable** after
`kexecve("/sbin/init")` because `kmain` panics if the scheduler returns.

QEMU GTK often delivers PS/2 scancodes on port `0x60` **without a reliable
IRQ1** while a userspace shell is blocked. With no poll in the wait path, **zero
scancodes reached the kernel** (serial log showed no `KBD_*` tags after
`SYS_READ_ENTER`).

**Fix:** While still `PROCESS_BLOCKED` after `sched_schedule_next()`, call
`kernel_idle_poll()` from:

- `tty_sleep_for_input()` in `includes/ir0/console.c`
- the blocking path of `sys_poll()` in `kernel/syscalls.c`

### 2. TTY echo and line discipline not tied to keypress time

Legacy `syscalls_read_stdio_stdin()` read the keyboard ring **without** going
through the TTY (no `ECHO`, no ICANON). Even after irinit `dup2(/dev/console)`,
echo had to happen when the read syscall processed bytes — too late if wake/poll
failed.

**Fix:**

- Route stdio stdin through `ir0_console_read()` → `tty_read_kernel()`
- On each PS/2 character, call `ir0_console_keypress()` from
  `keyboard_buffer_add()` — canonical line assembly + **immediate echo** to the
  framebuffer via `console_backend_write()`
- In ICANON mode, do not fill the raw keyboard ring (line lives in TTY canon
  buffers only)

## Data path (implemented)

```
QEMU PS/2 (port 0x60)
  → keyboard_poll_ps2() / IRQ1 handler
  → keyboard_feed_scancode() → keyboard_buffer_add()
  → ir0_console_keypress()
       → tty_canon_feed() + tty_echo_char()
       → console_backend_write() → typewriter → console_renderer → FB
  → (Enter) canon_readq ready
  → ir0_console_wake_readers() / stdin_wake_check()
  → ash read(0) returns line

ash write(1|2) → dev_console_write / console_backend → same FB path
```

While blocked on read, the wait loop is:

```
PROCESS_BLOCKED → sched_schedule_next()
  → if still blocked: kernel_idle_poll()
       → keyboard_poll_ps2()
       → stdin_wake_check() (+ poll_wake_check, …)
       → sched_schedule_next() when waiters woken
```

## Key source files

| Area | Path | Role |
|------|------|------|
| TTY wait + echo | `includes/ir0/console.c` | `tty_sleep_for_input`, `ir0_console_keypress`, canon echo |
| Keyboard | `interrupt/arch/keyboard.c` | PS/2 poll, `keyboard_buffer_add`, userspace diag tags |
| Stdin syscall | `kernel/syscalls.c` | `syscalls_read_stdio_stdin` → `ir0_console_read`; poll wait poll |
| Console draw | `kernel/console_backend.c`, `drivers/video/*` | FB handoff, `typewriter_vga_print_char` |
| devfs console | `fs/devfs.c` | `/dev/console`, `/dev/tty`, termios ioctl |
| PID 1 | `setup/pid1/irinit.c` | Attach stdio to `/dev/console`, spawn `ash -i` |
| BusyBox config | `setup/busybox/fase50_minimal.config` | `FEATURE_EDITING=n`, `ASH_JOB_CONTROL=n` |
| Smoke tags | `includes/ir0/ash_smoke.c` | Compact serial tags after BusyBox banner (FASE58K) |
| Next BusyBox | `setup/busybox/fase58_busybox.config` | More coreutils; build with `make build-busybox-fase58-plus` |

## BusyBox configuration (intentional)

For IR0's minimal TTY (no job control, no lineedit):

```
CONFIG_ASH_JOB_CONTROL=n
CONFIG_FEATURE_EDITING=n
```

Ash uses blocking `read(0)` in cooked mode; the **kernel** provides echo and
canonical line editing. Re-enabling `FEATURE_EDITING` requires a working
`poll` + raw TTY path and is out of scope for FASE58E.

## Build and run

```bash
make kernel-x64.bin
make kernel-x64-userspace.iso build-irinit build-busybox-fase50-min
make run-fase58e-ash-gui
```

Optional SDL display:

```bash
make run-fase58e-ash-gui FASE58E_DISPLAY=sdl
```

**Important:** Click the QEMU window to give it keyboard focus before typing.

Serial log (default):

```text
/tmp/fase58e-ash-gui.log
make check-fase58e-logs
```

Rebuild the **ISO** after kernel changes; QEMU loads the ISO, not a bare
`kernel-x64.bin`.

## Validation gates

```bash
make -s kernel-x64.bin
python3 scripts/architecture_guard.py
make -s kernel-x64-userspace.iso build-irinit build-busybox-fase50-min
```

Manual pass criteria:

1. `#` prompt visible after BusyBox banner
2. Typed letters visible on FB before Enter
3. `pwd` or `echo hi` produces output on FB
4. Serial shows compact smoke tags (see below)

Automated smoke (headless + QEMU monitor `sendkey`):

```bash
make smoke-fase58e-ash-interactive
make check-fase58e-logs   # also greps /tmp/fase58e-ash-smoke.log when present
```

If monitor key injection fails on your host, use GUI manual smoke:

```bash
make run-fase58e-ash-gui
# type: echo hi, pwd, ls
make check-fase58e-logs
```

## Serial diagnostic tags (FASE58K — compact)

Only these tags are emitted after the BusyBox banner (no per-character spam):

| Tag | Meaning |
|-----|---------|
| `ASH_INTERACTIVE_READY` | irinit spawned ash on console (from irinit) |
| `KBD_USER_POLL_OK` | First PS/2 byte polled in userspace phase |
| `TTY_CANON_LINE_READY` | Enter completed a canonical line |
| `SYS_READ_RETURN_OK` | read(0) returned bytes to ash (capped) |
| `ASH_COMMAND_ECHO_OK` | Shell stdout contained `hi` from `echo hi` |
| `ASH_COMMAND_EXEC_OK` | Shell stdout contained `/` line from `pwd` |

`#UD` faults still log `UD_FAULT_*` tags via `includes/ir0/fase58j_diag.c`.

If you see the prompt but **no** `KBD_USER_POLL_OK` when typing, the guest is
not receiving keys (QEMU focus or host input), not an echo bug.

## Known limits (not regressions)

- No kernel idle thread yet — TTY/poll blocking paths must call
  `kernel_idle_poll()` explicitly until an idle task is wired.
- Minimal BusyBox: only applets enabled in `fase50_minimal.config` exist.
- `stderr` prompt (`# `) uses the same FB console as stdout; color `0x0F`.
- Doom autostart is disabled in irinit production path (`DOOM_AUTOSTART_DISABLED`).
- FASE58K removed verbose FASE58G/I/J post-banner tracing; use compact tags above.

## Related documentation

- [FASE57 reintegration plan](fase57-reintegration-plan.md) — Step 58A console
  visibility and devfs fd binding
- [Virtual filesystems](VIRTUAL_FILESYSTEMS.md) — `/dev/console`, devfs
- [Interrupts](INTERRUPTS.md) — IRQ1 / PS/2 (high level)

## Tier note

This slice advances **T1 ~interactive init + ash** on real framebuffer console.
It does **not** complete musl, full applet coverage, or job control.
