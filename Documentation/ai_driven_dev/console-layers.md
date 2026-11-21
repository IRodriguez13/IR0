# Console layers: tty, console, video driver

> **Last verified:** 2026-11-21
> **Source of truth:** `drivers/tty/console.c`, `drivers/tty/console_backend.c`,
> `drivers/char/vga.c`, `drivers/video/{console,vt,fbcon,textcon,vbe,fb}.c`,
> `kernel/printk/{klog,console}.c`, `kernel/syscalls/fs_rw.c`

Decision Q8: split the text console into three layers, as Linux does:

| Layer | Linux | Owns |
|-------|-------|------|
| tty | `drivers/tty/tty_io.c`, `n_tty.c` | Line discipline, termios, job control, readers |
| console | `kernel/printk` `struct console`, `drivers/tty/vt/vt.c` | printk sinks; VT: cell buffer, ANSI/CSI, cursor, scrollback |
| video driver | `struct consw` (`vgacon`, `fbcon`), fbdev | Draw cells or pixels, scroll, clear |

## Current call graph

Who calls whom after steps 1–4 (below), file by file.

| Caller | Calls | Notes |
|--------|-------|-------|
| `fs/devfs_console.c` (`/dev/console`, `/dev/tty`) | `ir0_console_read/write` | tty entry points |
| `kernel/syscalls/fs_rw.c` (fd 1/2 not redirected) | `console_backend_write`, `console_backend_write_serial` | Bypasses the tty layer (no OPOST/ONLCR) |
| `arch/x86-64/.../keyboard.c` | `ir0_console_keypress` | Input into the line discipline |
| `drivers/tty/console.c` (tty: termios, canon, ISIG, fg pgrp, waiters) | `console_backend_write` (echo, output), `console_get_width/height/geometry` (TIOCGWINSZ), `console_backend_userspace_handoff` | tty reaches into the video console for window size |
| `drivers/tty/console_backend.c` (facade) | `vt_*`, `console_init`, `console_use_framebuffer`, `serial_putchar`, `klog_smoke`, `register_console` | Mixes `printk_to_screen`, tty serial mirror, handoff smoke tags; defines the VT printk console |
| `kernel/printk/klog.c` | `console_write(CON_RECORDS / CON_MIRROR / CON_DUMP)` | Knows no driver |
| `kernel/printk/console.c` | `serial_print`; registered `write` hooks | Registry; serial console registered statically |
| `fs/minix/minix_dir.c` | `klog_error`, `klog_warn` | |
| `drivers/char/vga.c` (`print`, `console_puts`, `print_colored`, `clear_screen`, `print_screen_only`) | `serial_putchar`, `vt_putchar_attr`, `vt_clear` | Serial half unchanged; screen half is the VT |
| `drivers/video/vt.c` (CSI/SGR, DECTCEM, alt screen, soft cursor, scrollback, `cursor_pos`) | `console_put_cell`, `console_draw_cell`, `console_get_cell`, `console_scroll_up`, `console_clear` | Single text writer; `<ir0/vt.h>` |
| `drivers/video/console.c` (cell shadow, geometry) | `struct console_video_ops` hooks | Console core |
| `drivers/video/fbcon.c` | `vbe_get_info/pitch/fb`, `font_terminus_14x28` | Pixel backend (32 bpp) |
| `drivers/video/textcon.c` | `struct console_text_screen` cells | Text-mode backend; platform registers the screen (`arch/x86-64/sources/boot_arch.c`: VGA 0xB8000) |
| `drivers/video/vbe.c`, `drivers/video/fb.c`, `video_backend.c` | firmware framebuffer | `/dev/fb0`, boot framebuffer; `vbe_init` falls back to the text screen |

Serial: `drivers/serial/serial.c` (x86 COM1), `drivers/tty/arm_serial.c` +
`pl011.c` (arm64). klog reaches serial through the serial printk console; tty
output is mirrored to serial by `console_backend_write` while
`tty_serial_mirror` is on.

Smoke tags owned by this path (strings and timing must not change):
`CONSOLE_FB_BACKEND_ENABLED` (`console_init`), `CONSOLE_BACKEND_FB_OK` /
`CONSOLE_BACKEND_VGA_OK`, `PRINTK_SERIAL_CONSOLE_FB_HANDOFF_OK`,
`CONSOLE_GUI_VISIBLE_OK` (handoff), `USERSPACE_FB_TEXT_FIRST_DRAW_OK` (first
userspace draw).

## Target split

| Layer | Files (target) | Interface it exports | Calls down into |
|-------|----------------|----------------------|-----------------|
| tty | `drivers/tty/console.c` (later `tty.c`) | `ir0_console_read/write/ioctl_*`, `ir0_console_keypress` | `vt_putchar`, `console_get_geometry`, serial console for the mirror |
| console: printk | `kernel/printk/console.c` | `struct console`, `register_console()`, `console_write()` | serial console, VT console |
| console: VT | `drivers/video/vt.c` + `console.c` core | `<ir0/vt.h>`, `console_get_geometry` | `struct console_video_ops` |
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
};
```

`console_init()` probes `fbcon_video_ops` then `textcon_video_ops`; the
first whose `init` accepts the screen draws it, and its geometry is cached.
The core keeps the cell shadow; backends never read it.

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
| vt | `CON_MIRROR`; draws only while `printk_to_screen` | `console_backend_init()` |

`CON_RECORDS`: every klog record, smoke tag and kprintf line (after the boot
hold). `CON_MIRROR`: records `klog_should_mirror_screen()` accepts.
`CON_DUMP`: raw panic-dump text (`klog_print`, `klog_hex32`, `klog_hex64`).

## Migration steps

1. **Done.** `struct console_video_ops`; FB and text-mode backends moved to
   `fbcon.c` / `textcon.c`; the console core dispatches through the ops.
2. **Done.** `print()` family draws through `vt_putchar_attr()` (caller's
   colour, SGR state kept); `cursor_pos` is static in `vt.c`. Screen-only
   differences: soft cursor during kernel text, BS erases, scroll fill uses
   the text colour, kernel lines reach the scrollback.
3. **Done.** `console_renderer.c` + `typewriter.c` merged into `vt.c`
   (`vt_*`); never-enabled delay modes removed; minix errors go to klog
   (serial now shows them as `[ERROR] [MINIX]` records).
4. **Done.** printk console registry; `klog.c` includes no serial, backend or
   VT header; the never-installed panic screen sink is replaced by
   `CON_DUMP`.
5. Split `console_backend.c`: `printk_to_screen` and the handoff tags go to
   the console layer, the tty serial mirror goes to the tty layer.
6. tty writes through the VT only; `fs_rw.c` stdio fast path goes through
   the tty write path (OPOST/ONLCR applied once).
7. Optional: hardware cursor hook in `console_video_ops` (VGA CRTC cursor);
   move VT files under `drivers/tty/vt/`.

Each step keeps the smoke tags above and the serial byte stream unchanged
unless the step says otherwise.
