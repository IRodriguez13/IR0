# IR0 Roadmap: Doom on IR0

Objetivo: ejecutar Doom en IR0 de forma segura y funcional, siguiendo principios Unix (simple, predecible) sin copiar complejidad de Linux.

## Dispositivos /dev (OSDev / Unix)

| Dispositivo | Descripción |
|-------------|-------------|
| /dev/null | Descartar datos |
| /dev/zero | Leer ceros |
| /dev/console | Teclado + VGA |
| /dev/tty | Alias de console |
| /dev/kmsg | Buffer de mensajes del kernel |
| /dev/audio | Sound Blaster: PCM 11025 Hz 8-bit mono (Doom); ioctl AUDIO_SET_FORMAT |
| /dev/mouse | PS/2 mouse (read, ioctl) |
| /dev/net | Red (ioctl: ping, config) |
| /dev/disk | Disco agregado |
| /dev/hda, /dev/hda1... | Discos y particiones |
| /dev/random, /dev/urandom | Entropía |
| /dev/full | Siempre ENOSPC |
| /dev/fb0 | Framebuffer VBE (write, ioctl FBIOGET_VSCREENINFO) |
| /dev/ipc | Canales IPC |
| /dev/bluetooth/hci0 | Bluetooth HCI |
| /dev/swap | Swap (ioctl: mkswap, swapon, swapoff) |

## Referencias Unix

- **BSD**: dispositivos simples, semántica clara, sin capas innecesarias
- **Plan 9**: todo es un archivo, interfaces mínimas
- **fbDOOM**: port de Doom que usa solo framebuffer + stdlib, sin SDL

## Requisitos mínimos de Doom

| Componente    | fbDOOM / Doom clásico | IR0 actual |
|---------------|------------------------|------------|
| Framebuffer   | 320x200 o 640x480      | No expuesto |
| open/read     | WAD files              | OK (VFS)    |
| Keyboard      | Input                  | OK (/dev/console) |
| Memoria       | ~4-8 MB                | OK (256MB heap)   |
| ELF estático  | ET_EXEC                | OK         |

## Fases (orden de prioridad)

### Fase 1: Base funcional (hecho / en curso)

- [x] sys_exec resuelve rutas relativas (como open/stat)
- [x] validate_path permite `..` y `//` (Unix normaliza)
- [x] sys_read/write retornan -EFAULT correctamente

### Fase 2: Framebuffer para usuariospace ✓

**Implementado**: `/dev/fb0` con VBE vía Multiboot (OSDev).

- **Multiboot framebuffer**: GRUB con `gfxpayload=1024x768x32` proporciona fb
- **vbe_init_from_multiboot()**: parsea info, mapea memoria si fb > 32MB
- **Fallback**: VGA text (80x25) si no hay framebuffer
- **API**: `write()` píxeles, `ioctl(FBIOGET_VSCREENINFO)` para width/height/bpp

### Fase 3: Semántica exec

**Situación actual**: sys_exec crea un proceso nuevo (spawn) y devuelve PID. El caller sigue ejecutando.

**Para Doom**:
- Opción A: Mantener spawn. Launcher hace `exec("doom"); waitpid(pid);` — patrón tipo posix_spawn
- Opción B: Hacer exec que reemplace proceso (como Unix) — cambio grande

**Recomendación**: Opción A por ahora. Documentar que `exec` en IR0 = spawn + carga ELF. El shell/launcher espera con waitpid.

### Fase 4: Stack y ABI

- Añadir `argc` en stack para crt0 estándar (si hace falta para el port de Doom)
- Mantener rdi=argc, rsi=argv, rdx=envp (x86-64 ABI)

### Fase 5: Opcionales (post-Doom)

- PT_GNU_STACK (stack no ejecutable por defecto)
- Página cero en 0x0
- PT_INTERP / binarios dinámicos (no necesario para Doom estático)

## Comparación con kernels Unix

| Aspecto        | Linux        | BSD          | IR0 (objetivo)     |
|----------------|-------------|--------------|--------------------|
| exec           | Reemplaza   | Reemplaza    | Spawn (documentado) |
| /dev/fb        | mmap + ioctl| Similar      | write + ioctl       |
| Rutas          | Normaliza   | Normaliza    | Normaliza           |
| Complejidad    | Alta        | Media        | Baja               |

## Próximos pasos concretos

1. Implementar `/dev/fb0` con VGA mode 13h (320x200) o framebuffer multiboot
2. Añadir entrada en grub.cfg para modo gráfico cuando se use fb
3. Port o adaptar fbDOOM para IR0 (framebuffer + open/read para WAD)
