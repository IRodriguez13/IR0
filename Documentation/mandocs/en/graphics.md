# IR0 Graphics and Framebuffer

| Field | Value |
|-------|-------|
| Version | 0.1 |
| IR0 phase | T2 |
| Status | stable |
| Depends on | memory, boot, devfs |
| Man page | IR0-graphics (section 7) |
| Primary sources | `kernel/video_backend.c`, `drivers/video/vbe.c`, `includes/ir0/fb.c`, `fs/devfs.c`, `kernel/syscalls.c`, `setup/doom/doomgeneric_ir0.c` |

## 1. Overview

Graphics support centers on a linear framebuffer from Multiboot/VBE, exposed as
`/dev/fb0` and mappable into userspace via `mmap`. Kernel text console uses the
same physical FB through `console_renderer` (80×25 logical grid, optional 2× glyph
scale). DoomGeneric is the reference T2 client: open fb0 + events0 + mmap.

## 2. Internal architecture

| Layer | File | Role |
|-------|------|------|
| Facade | `kernel/video_backend.c` | Wraps VBE; gated on `CONFIG_ENABLE_VBE` |
| Hardware | `drivers/video/vbe.c` | Multiboot fb init, VGA 0xB8000 fallback |
| FB API | `includes/ir0/fb.c` | Availability check, var/fix screeninfo |
| devfs | `fs/devfs.c` | `/dev/fb0` device_id **15**, ioctl FBIO* |
| mmap | `kernel/syscalls.c` | Special case maps physical FB pages |
| Console draw | `drivers/video/console_renderer.c` | CSI/SGR, cells to FB or VGA |
| Client | `setup/doom/doomgeneric_ir0.c` | T2 userspace blit loop |

## 3. Data flow

**Boot:**

```text
  kmain → video_backend_init_from_multiboot(mb_info)
       → vbe_init_from_multiboot OR fallback VGA text (0xB8000)
  boot_x64.asm may pre-map 0xFD000000 FB window
```

**Userspace client (Doom):**

```text
  open("/dev/fb0") → ioctl(FBIOGET_VSCREENINFO/FSCREENINFO)
       → mmap(MAP_SHARED, fd_fb, 0)  → sys_mmap maps phys FB pages
  open("/dev/events0") → read input_event loop
  DG_DrawFrame → write pixels to mmap buffer
```

**Kernel console (not Doom):**

```text
  write /dev/console → typewriter → console_renderer → console_put_cell
       → FB scaled glyphs OR VGA text buffer
```

ASCII:

```text
  Multiboot FB ──► vbe.c ──► ir0_fb_* ──► /dev/fb0
                                    │
                    ┌───────────────┼───────────────┐
                    ▼               ▼               ▼
              sys_mmap         console_renderer   doom mmap blit
              (userspace)      (kernel TTY)       (T2 client)
```

## 4. Responsibilities

- `ir0_fb_is_available()` rejects VGA text pseudo-fb (phys 0xB8000), requires min 320×200.
- mmap validates page-aligned offset, clamps to fb size, records VMA on `mmap_list`.
- Kernel console and fullscreen client share hardware but use different render paths.

## 5. Subsystem boundaries

- Use `includes/ir0/video_backend.h`, `includes/ir0/fb.h` from portable code.
- VGA fallback is for early boot/console, not full-screen clients expecting linear FB.
- IWAD for Doom not bundled; external path via Makefile `REAL_WAD_PATH`.

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|---------------|
| Memory | `map_user_region_in_directory` for FB mmap |
| Boot | Multiboot info, early page tables |
| Input | events0 paired with fb0 in clients |
| TTY | typewriter shares console_renderer |
| Userspace | fase55 Doom smokes, musl static build |

## 7. Visual maps

```text
  [GRUB multiboot] ──► [VBE init] ──► linear FB phys
                                           │
              ┌────────────────────────────┼────────────────────┐
              ▼                            ▼                    ▼
        /dev/fb0 ioctl              kernel 80x25 text      mmap userspace
```

## 8. Important invariants

1. `CONFIG_ENABLE_VBE` required for linear fb and mmap path.
2. Default console FB scale 2× (needs ~1280×800); falls back to 1× if too small.
3. `ir0_fb_write_bytes` supports 1–4 bytes/pixel formats.
4. Read on fb0 always returns 0 (write-only from userspace perspective).
5. fb0 mode 0660 on devfs.

## 9. Debugging tips

- `[BOOT] vbe_fail_reason=`: 1=mb_null, 2=no_fb, 3=bad_dims, 4=map_fail.
- Smoke: `build-init-fase54a-fbdev`, `run-fase55d-doomgeneric-gui`.
- Blank GUI: verify userspace ISO + VBE config, not debug-shell-only ISO.
- mmap fails: check fd is fb0 (device_id 15) and VBE enabled.

## 10. Future roadmap

- Double buffering / vsync — client polls directly today.
- Multiple fb devices — single fb0 only.
- GPU acceleration — not in scope.
- Wayland/compositor — T3 out of kernel tree.

See: `IR0-memory` (mmap), `IR0-input`, `IR0-tty`.
