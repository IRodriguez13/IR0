# Documentación interna IR0 — Índice Mandoc

| Campo | Valor |
|-------|-------|
| Versión | 0.4 |
| Fase IR0 | T0–T2 (transversal) |
| Estado | stable |
| Página man | (solo navegación — usar `IR0-<slug>` por subsistema) |

## Propósito

Documentación bilingüe fiel al código en `Documentation/mandocs/`.
Empezá por: `make man TOPIC=onboarding`.

**Nota de oleada (0.4):** `IR0-onboarding`; boot log opcional → virtio-9p
(`BOOT_LOG_HOSTSHARE` / `make run-bootlog`); mapa honesto de facades abajo.

## Índice de capítulos

| Slug | Man page | Tier | Estado | Facades primarias (`includes/ir0/`) |
|------|----------|------|--------|-------------------------------------|
| onboarding | IR0-onboarding | T0 | stable | (entrada — `boot_log_hostshare.h`) |
| boot | IR0-boot | T0 | stable | `boot_log.h`, `arch_port.h` |
| vfs | IR0-vfs | T0 | stable | `path_routed`, facades VFS |
| scheduler | IR0-scheduler | T0 | stable | `sched.h` |
| memory / mm | IR0-memory | T0–T1 | stable | `mm_port.h` (ver también `mm.md`) |
| syscalls | IR0-syscalls | T0–T1 | stable | `copy_user`, open_flags |
| filesystems | IR0-filesystems | T0 | stable | `virtio_9p.h`, blockdev |
| tty | IR0-tty | T1–T2 | stable | `console.h` |
| drivers | IR0-drivers | T0 | stable | `*_backend.h` |
| process | IR0-process | T1 | stable | process / signals |
| userspace | IR0-userspace | T1–T2 | stable | exec / ash |
| multi-arch | IR0-multi-arch | T0 | stable | `arch_port.h`, `arm64_board.h` |
| net | IR0-net | T0 | stable | `net.h` |
| interrupts | IR0-interrupts | T0 | stable | irq / arch port |
| ipc | IR0-ipc | T0–T1 | stable | `pipe.h` |
| input | IR0-input | T2 | stable | `input.h` |
| graphics | IR0-graphics | T2 | stable | `fb.h`, `video_backend.h` |
| debug-bins | IR0-debug-bins | T0 | stable | debug shell |
| signals | IR0-signals | T1 | stable | `signals.h` |
| security | IR0-security | T0–T1 | stable | cred / sudo |

**Aún no cubierto (honesto):** ~119 headers en `includes/ir0/`; los mandocs
cubren rodajas de subsistema, no una página por header.

`memory.md` es el capítulo man; `mm.md` es compañero COW/profundidad.

## Compilación

```bash
make sync-mandocs
make man TOPIC=onboarding
```

Ver también: [SETUP.md](../../SETUP.md), [README.md](../../README.md).
