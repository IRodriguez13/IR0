# Documentación interna IR0 — Índice Mandoc

| Campo | Valor |
|-------|-------|
| Versión | 0.3 |
| Fase IR0 | T0–T2 (transversal) |
| Estado | stable |
| Página man | (solo navegación — usar `IR0-<slug>` por subsistema) |

## Propósito

Documentación bilingüe fiel al código en `Documentation/mandocs/`.
Estudiar IR0 desde el sistema: `man IR0-vfs-es`, `man IR0-net-es`, etc.

**Nota de oleada (0.3):** documentados AF_INET TCP wire + teardown FIN/EOF; ARM
F7h–F7j process/TTBR freestanding; symlink virtio-9p; planificador priority por
defecto; fachada `arch_first_context_switch`.

## Índice de capítulos

| Slug | Man page | Tier | Estado |
|------|----------|------|--------|
| vfs | IR0-vfs | T0 | stable |
| boot | IR0-boot | T0 | stable |
| scheduler | IR0-scheduler | T0 | stable |
| memory | IR0-memory | T0–T1 | stable |
| syscalls | IR0-syscalls | T0–T1 | stable |
| filesystems | IR0-filesystems | T0 | stable |
| tty | IR0-tty | T1–T2 | stable |
| drivers | IR0-drivers | T0 | stable |
| process | IR0-process | T1 | stable |
| userspace | IR0-userspace | T1–T2 | stable |
| multi-arch | IR0-multi-arch | T0 | stable |
| net | IR0-net | T0 | stable |
| interrupts | IR0-interrupts | T0 | stable |
| ipc | IR0-ipc | T0–T1 | stable |
| input | IR0-input | T2 | stable |
| graphics | IR0-graphics | T2 | stable |
| debug-bins | IR0-debug-bins | T0 | stable |
| signals | IR0-signals | T1 | stable |
| security | IR0-security | T0–T1 | stable |

Plantilla: `Documentation/mandocs/TEMPLATE.md`. Regla Cursor: `ir0-mandocs-initiative`.

## Compilación

```bash
make mandocs-es
man IR0-net-es
python3 scripts/build_mandocs.py --lang es --mandoc-only --no-install
```

Diagramas **ASCII inline en cada capítulo** (compatible mandoc).

Ver también: [SETUP.md](../../SETUP.md), [Documentation/esp/README.md](../../esp/README.md).
