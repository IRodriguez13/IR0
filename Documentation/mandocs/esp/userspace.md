# Bootstrap de userspace de IR0

| Campo | Valor |
|-------|-------|
| Versión | 0.1 |
| Fase IR0 | T1–T2 |
| Estado | stable |
| Depende de | boot, process, vfs, tty |
| Página man | IR0-userspace (sección 7) |
| Fuentes principales | `setup/pid1/irinit.c`, `kernel/main.c`, `kernel/rootfs_base.c`, `scripts/inject_init_minix.py`, `Makefile` |

## 1. Visión general

El arranque de producción carga **`/sbin/init`** vía `kexecve` desde `kmain`
cuando `KERNEL_DEBUG_SHELL=0`. La implementación de referencia PID1 es
**irinit** (`setup/pid1/irinit.c`), compilada estática con musl e inyectada en
la imagen raíz MINIX. BusyBox, TCC y DoomGeneric son payloads opcionales de
rootfs para smoke e hitos gráficos T2.

## 2. Arquitectura interna

| Artefacto | Rol |
|-----------|-----|
| `irinit.c` | PID1: attach consola, spawn `/bin/sh`, reap zombies |
| `init_musl.c` | binario smoke syscalls musl |
| `rootfs_base.c` | Crea `/bin`, `/sbin`, `/dev`, `/proc`, … en disco |
| `inject_init_minix.py` | Escribe binarios en imagen MINIX v1 |
| `busybox-1.36.1` | Applets de terceros; configs en `setup/busybox/` |
| `kernel-x64-userspace.bin` | Kernel compilado con `IR0_USERSPACE_INIT_BOOT=1` |

**Comportamiento irinit (no-smoke):** preparar entorno → attach consola → spawn shell →
respawn al salir; límites en SEGV consecutivos y bucles shell vacíos.

## 3. Flujo de datos

```text
  make kernel-x64-userspace.iso + disk.img
       │
       ▼
  inject_init_minix.py  (irinit → /sbin/init, busybox → /bin/...)
       │
       ▼
  QEMU: kernel-x64-userspace.iso + disk.img
       │
       ▼
  kmain → vfs_init_root (MINIX /)
       → ir0_rootfs_prepare_userspace_base()  (mkdir layout)
       → kexecve("/sbin/init")
       │
       ▼
  irinit → open /dev/console → spawn /bin/sh (BusyBox ash)
       │
       ▼
  user: doom, tcc, smokes coreutils
```

ASCII:

```text
  [disco MINIX]          [ISO kernel]
  /sbin/init=irinit  +  flag boot userspace
         │                    │
         └────────┬───────────┘
                  ▼
            kexecve("/sbin/init")
                  ▼
              irinit → /bin/sh
```

## 4. Responsabilidades

- Kernel: montar raíz, asegurar dirs base, exec init una vez, luego schedule.
- irinit: reap zombies, respawn shell, sin acceso a drivers (solo syscalls).
- Sistema de build: cross compiler musl (`MUSL_CC`), scripts inject, targets ISO.

## 5. Límites del subsistema

- PID1 no debe enlazar símbolos kernel; solo musl estático.
- Shell `debug_bins` reemplaza init cuando `KERNEL_DEBUG_SHELL=1` — ruta T0 separada.
- Inits con nombre de fase bajo `setup/pid1/init_fase*.c` son harness smoke, no PID1 de producción.

## 6. Relaciones con otros subsistemas

| Vecino | Interacción |
|--------|-------------|
| Boot | handoff `kexecve` |
| VFS | raíz MINIX, fallback tmpfs si falta disco |
| TTY | irinit usa `/dev/console`, termios |
| Process | fork/exec/wait en shell y applets |
| T2 | DoomGeneric vía layout `/usr/share/doom` en rootfs_base |

## 7. Mapas visuales

```text
  cadena build:
  musl-gcc → binarios irinit/busybox/doom
       → inject_init_minix.py → disk.img
       → make kernel-x64-userspace.iso
       → targets smoke QEMU
```

## 8. Invariantes importantes

1. `/sbin/init` debe existir en FS raíz para la ruta de arranque de producción.
2. `IR0_USERSPACE_INIT_BOOT=1` requerido en kernel para init real (no debug shell).
3. Modo smoke irinit (`DIRINIT_SMOKE`) se detiene tras probes — no es default interactivo.
4. musl requiere `x86_64-linux-musl-gcc` o `musl-gcc`.

## 9. Consejos de depuración

| Síntoma | Arreglo |
|---------|---------|
| `musl cross compiler not found` | `apt install musl-tools`, fijar `MUSL_CC` |
| `/sbin/init` not found | Re-ejecutar inject; comprobar disk.img |
| Shell kernel en lugar de init | ISO incorrecta (usar variante userspace) |
| ash silencioso | Ver IR0-tty; verificar `/dev/console` |

Targets: `make build-irinit`, `make smoke-userspace-shell`, `make run-irinit-interactive-gui`.

Ver `SETUP.md` para el flujo completo de bootstrap.

## 10. Hoja de ruta futura

- Supervisión estilo runit — irinit es mínimo, no port runit completo.
- Enlace dinámico musl — solo binarios estáticos hoy.
- Layout de paquetes / libs compartidas en rootfs adecuado — staging parcial `/lib`.
- Init estilo systemd — **fuera de alcance** para IR0 T1.

Doc de fase: `Documentation/fase58e-ash-interactive-console.md`.
