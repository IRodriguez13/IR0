# Gráficos y framebuffer de IR0

| Campo | Valor |
|-------|-------|
| Versión | 0.1 |
| Fase IR0 | T2 |
| Estado | stable |
| Depende de | memory, boot, devfs |
| Página man | IR0-graphics (sección 7) |
| Fuentes principales | `kernel/video_backend.c`, `drivers/video/vbe.c`, `includes/ir0/fb.c`, `fs/devfs.c`, `kernel/syscalls.c`, `setup/doom/doomgeneric_ir0.c` |

## 1. Visión general

El soporte gráfico se centra en un framebuffer lineal desde Multiboot/VBE, expuesto
como `/dev/fb0` y mapeable en userspace vía `mmap`. La consola de texto del kernel
usa el mismo FB físico mediante `console_renderer` (rejilla lógica 80×25, escala
de glifo 2× opcional). DoomGeneric es el cliente de referencia T2: open fb0 +
events0 + mmap.

## 2. Arquitectura interna

| Capa | Archivo | Rol |
|------|---------|-----|
| Fachada | `kernel/video_backend.c` | Envuelve VBE; condicionado a `CONFIG_ENABLE_VBE` |
| Hardware | `drivers/video/vbe.c` | Init fb Multiboot, fallback VGA 0xB8000 |
| API FB | `includes/ir0/fb.c` | Comprobación disponibilidad, var/fix screeninfo |
| devfs | `fs/devfs.c` | `/dev/fb0` device_id **15**, ioctl FBIO* |
| mmap | `kernel/syscalls.c` | Caso especial mapea páginas FB físicas |
| Dibujo consola | `drivers/video/console_renderer.c` | CSI/SGR, celdas a FB o VGA |
| Cliente | `setup/doom/doomgeneric_ir0.c` | Bucle blit userspace T2 |

## 3. Flujo de datos

**Arranque:**

```text
  kmain → video_backend_init_from_multiboot(mb_info)
       → vbe_init_from_multiboot O fallback texto VGA (0xB8000)
  boot_x64.asm puede pre-mapear ventana FB 0xFD000000
```

**Cliente userspace (Doom):**

```text
  open("/dev/fb0") → ioctl(FBIOGET_VSCREENINFO/FSCREENINFO)
       → mmap(MAP_SHARED, fd_fb, 0)  → sys_mmap mapea páginas FB físicas
  open("/dev/events0") → bucle read input_event
  DG_DrawFrame → escribe píxeles en buffer mmap
```

**Consola kernel (no Doom):**

```text
  write /dev/console → typewriter → console_renderer → console_put_cell
       → glifos escalados FB O buffer texto VGA
```

Mapa ASCII:

```text
  FB Multiboot ──► vbe.c ──► ir0_fb_* ──► /dev/fb0
                                    │
                    ┌───────────────┼───────────────┐
                    ▼               ▼               ▼
              sys_mmap         console_renderer   blit mmap doom
              (userspace)      (TTY kernel)       (cliente T2)
```

## 4. Responsabilidades

- `ir0_fb_is_available()` rechaza pseudo-fb texto VGA (phys 0xB8000), exige mín 320×200.
- mmap valida offset alineado a página, limita a tamaño fb, registra VMA en `mmap_list`.
- Consola kernel y cliente pantalla completa comparten hardware pero usan paths de render distintos.

## 5. Límites del subsistema

- Usar `includes/ir0/video_backend.h`, `includes/ir0/fb.h` desde código portable.
- Fallback VGA es para arranque temprano/consola, no clientes pantalla completa que esperan FB lineal.
- IWAD para Doom no incluido; path externo vía Makefile `REAL_WAD_PATH`.

## 6. Relación con otros subsistemas

| Vecino | Interacción |
|--------|-------------|
| Memory | `map_user_region_in_directory` para mmap FB |
| Boot | Info Multiboot, tablas de página tempranas |
| Input | events0 emparejado con fb0 en clientes |
| TTY | typewriter comparte console_renderer |
| Userspace | smokes fase55 Doom, build musl estático |

## 7. Mapas visuales

```text
  [GRUB multiboot] ──► [init VBE] ──► FB lineal phys
                                           │
              ┌────────────────────────────┼────────────────────┐
              ▼                            ▼                    ▼
        ioctl /dev/fb0              texto kernel 80x25      mmap userspace
```

## 8. Invariantes importantes

1. `CONFIG_ENABLE_VBE` requerido para fb lineal y path mmap.
2. Escala consola FB predeterminada 2× (necesita ~1280×800); fallback 1× si muy pequeño.
3. `ir0_fb_write_bytes` soporta formatos 1–4 bytes/píxel.
4. Read en fb0 siempre devuelve 0 (solo escritura desde perspectiva userspace).
5. fb0 mode 0660 en devfs.

## 9. Consejos de depuración

- `[BOOT] vbe_fail_reason=`: 1=mb_null, 2=no_fb, 3=bad_dims, 4=map_fail.
- Smoke: `build-init-fase54a-fbdev`, `run-fase55d-doomgeneric-gui`.
- GUI en blanco: verificar ISO userspace + config VBE, no solo ISO debug-shell.
- mmap falla: comprobar fd es fb0 (device_id 15) y VBE habilitado.

## 10. Roadmap futuro

- Double buffering / vsync — el cliente hace poll directo hoy.
- Múltiples dispositivos fb — solo fb0.
- Aceleración GPU — fuera de alcance.
- Wayland/compositor — T3 fuera del árbol kernel.

Ver: `IR0-memory` (mmap), `IR0-input`, `IR0-tty`.
