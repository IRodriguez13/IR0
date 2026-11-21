# Console layers: tty, console, video driver

> **Last verified:** 2026-11-21
> **Source of truth:** `drivers/tty/console.c`, `drivers/tty/console_backend.c`,
> `drivers/char/vga.c`, `drivers/video/{console,console_renderer,typewriter,fbcon,textcon,vbe,fb}.c`,
> `kernel/printk/klog.c`, `kernel/syscalls/fs_rw.c`

Decision Q8: split the text console into three layers, as Linux does:

| Layer | Linux | Owns |
|-------|-------|------|
| tty | `drivers/tty/tty_io.c`, `n_tty.c` | Line discipline, termios, job control, readers |
| console | `kernel/printk` `struct console`, `drivers/tty/vt/vt.c` | printk sinks; VT: cell buffer, ANSI/CSI, cursor, scrollback |
| video driver | `struct consw` (`vgacon`, `fbcon`), fbdev | Draw cells or pixels, scroll, clear |

## Current call graph

Who calls whom today, file by file (before the first migration step, the
console core in `drivers/video/console.c` also held both video backends).

| Caller | Calls | Notes |
|--------|-------|-------|
| `fs/devfs_console.c` (`/dev/console`, `/dev/tty`) | `ir0_console_read/write` | tty entry points |
| `kernel/syscalls/fs_rw.c` (fd 1/2 not redirected) | `console_backend_write`, `console_backend_write_serial` | Bypasses the tty layer (no OPOST/ONLCR) |
| `arch/x86-64/.../keyboard.c` | `ir0_console_keypress` | Input into the line discipline |
| `drivers/tty/console.c` (tty: termios, canon, ISIG, fg pgrp, waiters) | `console_backend_write` (echo, output), `console_get_width/height/geometry` (TIOCGWINSZ), `console_backend_userspace_handoff` | tty reaches into the video console for window size |
| `drivers/tty/console_backend.c` (facade) | `typewriter_*`, `console_init`, `console_use_framebuffer`, `serial_putchar`, `klog_smoke` | Mixes printk gating (`printk_to_screen`), tty serial mirror, handoff smoke tags |
| `kernel/printk/klog.c` | `serial_print` (every record); `typewriter_vga_print` (NOTICE+ when `printk_to_screen`); panic screen sink callback | printk knows the VT implementation |
| `fs/minix/minix_fs_internal.h` | `console_backend_write` via a `typewriter_vga_print` macro | FS prints to the screen |
| `drivers/char/vga.c` (`print`, `console_puts`, `print_colored`, `clear_screen`, `print_screen_only`) | `serial_putchar`, `console_put_cell`, `console_scroll_up`, `console_clear` | Second text writer with its own `cursor_x/y`, shares the global `cursor_pos` with the renderer |
| `drivers/video/typewriter.c` (scrollback ring, delay modes) | `console_renderer_putchar/reset/show_cursor`, `console_put_cell` | Delay modes are only ever set to `TYPEWRITER_DISABLED` |
| `drivers/video/console_renderer.c` (CSI/SGR, DECTCEM, alt screen, soft cursor) | `console_put_cell`, `console_draw_cell`, `console_get_cell`, `console_scroll_up`, `console_clear` | Uses `extern int cursor_pos` from `vga.c` |
| `drivers/video/console.c` (cell shadow, geometry) | `struct console_video_ops` hooks | Console core |
| `drivers/video/fbcon.c` | `vbe_get_info/pitch/fb`, `font_terminus_14x28` | Pixel backend (32 bpp) |
| `drivers/video/textcon.c` | `struct console_text_screen` cells | Text-mode backend; platform registers the screen (`arch/x86-64/sources/boot_arch.c`: VGA 0xB8000) |
| `drivers/video/vbe.c`, `drivers/video/fb.c`, `video_backend.c` | firmware framebuffer | `/dev/fb0`, boot framebuffer; `vbe_init` falls back to the text screen |

Serial: `drivers/serial/serial.c` (x86 COM1), `drivers/tty/arm_serial.c` +
`pl011.c` (arm64). klog writes serial directly; tty output is mirrored to
serial by `console_backend_write` while `tty_serial_mirror` is on.

Smoke tags owned by this path (strings and timing must not change):
`CONSOLE_FB_BACKEND_ENABLED` (`console_init`), `CONSOLE_BACKEND_FB_OK` /
`CONSOLE_BACKEND_VGA_OK`, `PRINTK_SERIAL_CONSOLE_FB_HANDOFF_OK`,
`CONSOLE_GUI_VISIBLE_OK` (handoff), `USERSPACE_FB_TEXT_FIRST_DRAW_OK` (first
userspace draw).

## Target split

| Layer | Files (target) | Interface it exports | Calls down into |
|-------|----------------|----------------------|-----------------|
| tty | `drivers/tty/console.c` (later `tty.c`) | `ir0_console_read/write/ioctl_*`, `ir0_console_keypress` | `vt_write(buf, len, color)`, `vt_get_winsize()`, serial console for the mirror |
| console: printk | console registry (new) | `struct console { name; write(const char *, size_t); flags; }`, `register_console()` | serial console, VT console |
| console: VT | `console.c` core + `console_renderer.c` + scrollback from `typewriter.c` | `vt_write`, `vt_clear`, `vt_scroll`, `console_get_geometry` | `struct console_video_ops` |
| video driver | `fbcon.c`, `textcon.c`, `vbe.c`, `fb.c` | `struct console_video_ops` | hardware |

Console/video boundary (in tree since step 1, `drivers/video/console.h`):

```c
struct console_video_ops
{
	int is_framebuffer;
	int (*init)(struct console_geometry *geo);
	void (*put_cell)(int row, int col, char c, uint8_t color);
	void (*scroll_up)(uint8_t clear_color);
	void (*clear)(uint8_t color);
};
```

`console_init()` probes `fbcon_video_ops` then `textcon_video_ops`; the
first whose `init` accepts the screen draws it, and its geometry is cached.
The core keeps the cell shadow; backends never read it.

## Migration steps

1. **Done.** `struct console_video_ops`; FB and text-mode backends moved to
   `fbcon.c` / `textcon.c`; the console core dispatches through the ops.
2. Single writer: route `print()`/`console_puts()` (`drivers/char/vga.c`)
   through the VT writer instead of `console_put_cell` with its own cursor;
   make `cursor_pos` private to the VT.
3. Fold the `typewriter.c` scrollback into the VT, drop the dead delay modes,
   rename `typewriter_*` to `vt_*`; replace the `typewriter_vga_print` macro
   in `fs/minix/minix_fs_internal.h` with klog.
4. printk console registry: serial console and VT console registered as
   `struct console`; `klog_event` writes to registered consoles; the panic
   screen sink becomes a console flag; `klog.c` stops including
   `typewriter.h`.
5. Split `console_backend.c`: printk gating and handoff tags go to the
   console registry, the tty serial mirror goes to the tty layer.
6. tty writes through a driver hook (`vt_write`) only; `fs_rw.c` stdio fast
   path goes through the tty write path (OPOST/ONLCR applied once).
7. Optional: hardware cursor hook in `console_video_ops` (VGA CRTC cursor)
   once the soft cursor lives in the VT; move VT files under `drivers/tty/vt/`.

Each step keeps the smoke tags above and the serial byte stream unchanged
unless the step says otherwise.
