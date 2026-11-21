# Console layers: tty, console, video driver

> **Last verified:** 2026-11-21
> **Source of truth:** `drivers/tty/{console,console_backend,tty_mirror}.c`,
> `drivers/tty/vt/vt.c`, `drivers/char/vga.c`,
> `drivers/video/{console,fbcon,textcon,vbe,fb}.c`,
> `kernel/printk/{klog,console}.c`, `kernel/syscalls/fs_rw.c`,
> `arch/x86-64/sources/boot_arch.c`

Decision Q8: split the text console into three layers, as Linux does:

| Layer | Linux | Owns |
|-------|-------|------|
| tty | `drivers/tty/tty_io.c`, `n_tty.c` | Line discipline, termios, job control, readers |
| console | `kernel/printk` `struct console`, `drivers/tty/vt/vt.c` | printk sinks; VT: cell buffer, ANSI/CSI, cursor, scrollback |
| video driver | `struct consw` (`vgacon`, `fbcon`), fbdev | Draw cells or pixels, scroll, clear |

## Current call graph

Who calls whom after steps 1–5 and 7 (step 6 left undone), file by file.

| Caller | Calls | Notes |
|--------|-------|-------|
| `fs/devfs_console.c` (`/dev/console`, `/dev/tty`) | `ir0_console_read/write` | tty entry points |
| `kernel/syscalls/fs_rw.c` (fd 1/2 not redirected) | `console_backend_write`, `console_backend_write_serial` | Still bypasses the tty (no OPOST/ONLCR). Graphical console: serial only |
| `arch/x86-64/.../keyboard.c` | `ir0_console_keypress` | Input into the line discipline |
| `drivers/tty/console.c` (tty: termios, canon, ISIG, fg pgrp, waiters) | `console_backend_write` (echo, output), `console_get_width/height/geometry` (TIOCGWINSZ), `console_backend_userspace_handoff` | tty reaches into the video console for window size |
| `drivers/tty/console_backend.c` (facade) | `vt_*`, `tty_serial_mirror_*` | Public entry points only. Does not own the screen gate or the mirror |
| `drivers/tty/tty_mirror.c` | `serial_putchar` | TTY serial mirror. LF becomes CR LF. Off after handoff when `CONFIG_CONSOLE_SERIAL_MIRROR` is off |
| `kernel/printk/klog.c` | `console_write(CON_RECORDS / CON_MIRROR / CON_DUMP)` | Knows no driver |
| `kernel/printk/console.c` | `serial_print`; registered `write` hooks | Registry; serial console registered statically |
| `fs/minix/minix_dir.c` | `klog_error`, `klog_warn` | |
| `drivers/char/vga.c` (`print`, `console_puts`, `print_colored`, `clear_screen`, `print_screen_only`) | `serial_putchar`, `vt_putchar_attr`, `vt_clear` | Serial half unchanged; screen half is the VT |
| `drivers/tty/vt/vt.c` (CSI/SGR, DECTCEM, alt screen, cursor, scrollback, `cursor_pos`, `printk_to_screen`, handoff tags) | `console_put_cell`, `console_draw_cell`, `console_get_cell`, `console_scroll_up`, `console_clear`, `console_set_cursor`, `register_console`, `klog_smoke` | Single text writer; `<ir0/vt.h>`. Registers the VT printk console (`CON_MIRROR`) |
| `drivers/video/console.c` (cell shadow, geometry) | `struct console_video_ops` hooks | Console core |
| `drivers/video/fbcon.c` | `vbe_get_info/pitch/fb`, `font_terminus_14x28` | Pixel backend (32 bpp) |
| `drivers/video/textcon.c` | `struct console_text_screen` cells and its `cursor` hook | Text-mode backend. The hook is optional |
| `arch/x86-64/sources/boot_arch.c` | VGA CRTC ports `0x3D4`/`0x3D5` | Registers the 0xB8000 screen and its hardware cursor |
| `drivers/video/vbe.c`, `drivers/video/fb.c`, `video_backend.c` | firmware framebuffer | `/dev/fb0`, boot framebuffer; `vbe_init` falls back to the text screen |

Serial: `drivers/serial/serial.c` (x86 COM1), `drivers/tty/arm_serial.c` +
`pl011.c` (arm64). klog reaches serial through the serial printk console; tty
output is mirrored to serial by `console_backend_write` while
`tty_serial_mirror` is on.

Smoke tags owned by this path (strings and timing must not change):
`CONSOLE_FB_BACKEND_ENABLED` (`console_init`), `CONSOLE_BACKEND_FB_OK` /
`CONSOLE_BACKEND_VGA_OK`, `PRINTK_SERIAL_CONSOLE_FB_HANDOFF_OK`,
`CONSOLE_GUI_VISIBLE_OK` (handoff), `USERSPACE_FB_TEXT_FIRST_DRAW_OK`
(first `console_backend_write` after the screen gate drops, which is the
handoff ready line on the framebuffer).

## Target split

| Layer | Files (target) | Interface it exports | Calls down into |
|-------|----------------|----------------------|-----------------|
| tty | `drivers/tty/console.c` (later `tty.c`) | `ir0_console_read/write/ioctl_*`, `ir0_console_keypress` | `vt_putchar`, `console_get_geometry`, serial console for the mirror |
| console: printk | `kernel/printk/console.c` | `struct console`, `register_console()`, `console_write()` | serial console, VT console |
| console: VT | `drivers/tty/vt/vt.c` + `drivers/video/console.c` | `<ir0/vt.h>`, `console_get_geometry` | `struct console_video_ops` |
| video driver | `fbcon.c`, `textcon.c`, `vbe.c`, `fb.c` | `struct console_video_ops` | hardware |

Console/video boundary (`drivers/video/console.h`):

```c
struct console_video_ops
{
	int is_framebuffer;
	int (*init)(struct console_geometry *geo);
	void (*put_cell)(int row, int col, char c, uint8_t color);
	void (*scroll_up)(uint8_t clear_color);
	void (*clear)(uint8_t color);
	/* Returns 1 when this backend placed a hardware cursor. */
	int (*cursor)(int row, int col, int on);
};
```

`console_init()` probes `fbcon_video_ops` then `textcon_video_ops`; the
first whose `init` accepts the screen draws it, and its geometry is cached.
The core keeps the cell shadow; backends never read it. `cursor` is optional:
the framebuffer backend leaves it NULL and the VT inverts the cell under the
cursor. The text backend forwards a non-NULL `console_text_screen.cursor`.
On PC VGA that hook programs the CRTC, and the VT does not also invert the
cell.

printk registry (`<ir0/printk_console.h>`):

```c
struct console
{
	const char *name;
	void (*write)(const char *str);
	unsigned int flags;	/* CON_RECORDS | CON_MIRROR | CON_DUMP */
	struct console *next;
};
```

| Console | Flags | Registered by |
|---------|-------|---------------|
| serial | `CON_RECORDS \| CON_DUMP` | static list head (`kernel/printk/console.c`) |
| vt | `CON_MIRROR`; draws only while `printk_to_screen` | `vt_register_printk_console()`, from `console_backend_init()` |

`CON_RECORDS`: every klog record, smoke tag and kprintf line (after the boot
hold). `CON_MIRROR`: records `klog_should_mirror_screen()` accepts.
`CON_DUMP`: raw panic-dump text (`klog_print`, `klog_hex32`, `klog_hex64`).

## Migration steps

1. **Done.** `struct console_video_ops`; FB and text-mode backends moved to
   `fbcon.c` / `textcon.c`; the console core dispatches through the ops.
2. **Done.** `print()` family draws through `vt_putchar_attr()` (caller's
   colour, SGR state kept); `cursor_pos` is static in `vt.c`. Screen-only
   differences at that step: soft cursor during kernel text (step 7
   replaces it on VGA text mode), BS erases, scroll fill uses the text
   colour, kernel lines reach the scrollback.
3. **Done.** `console_renderer.c` + `typewriter.c` merged into `vt.c`
   (`vt_*`); never-enabled delay modes removed; minix errors go to klog
   (serial now shows them as `[ERROR] [MINIX]` records).
4. **Done.** printk console registry; `klog.c` includes no serial, backend or
   VT header; the never-installed panic screen sink is replaced by
   `CON_DUMP`.
5. **Done.** `printk_to_screen`, the VT printk console and the handoff tags
   live in `drivers/tty/vt/vt.c`. The TTY serial mirror lives in
   `drivers/tty/tty_mirror.c`. `console_backend.c` keeps the public entry
   points and the same call order. Serial bytes unchanged.
6. **Not done.** fd 1/2 and the tty write path do not emit the same serial
   bytes, so one ONLCR policy cannot keep both. Unredirected fd 1/2
   (`fs_rw.c`) always expands LF to CR LF and ignores termios; on a
   graphical console it writes serial only and does not draw. A tty write
   with the default ONLCR|OPOST inserts CR, then `console_backend_write`
   inserts another, so LF leaves the serial port as CR CR LF (shell and
   `/dev/console`). Applying ONLCR once would change one of those streams.
   Left as it is. Smoke tags are unchanged and still come from the handoff
   ready line.
7. **Done.** `cursor` hook on `console_video_ops`; PC VGA CRTC cursor in
   `arch/x86-64/sources/boot_arch.c`. VT sources live in `drivers/tty/vt/`.
   VGA text mode shows the hardware cursor instead of an inverted cell.
   The framebuffer console still inverts the cell. Serial bytes unchanged.

Each step keeps the smoke tags above and the serial byte stream unchanged
unless the step says otherwise.
