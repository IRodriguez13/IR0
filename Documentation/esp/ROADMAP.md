# IR0 Kernel — Roadmap consolidado de desarrollo

> **Última verificación:** 2026-06-23 (release **0.0.1** — ver [`STABLE.md`](STABLE.md))  
> **Fuente de verdad:** código en `kernel/`, `mm/`, `sched/`, `fs/`, `net/`, `setup/`, `ktm/`, `scripts/` y gates CTR en `Makefile`.

Documento consolidado: tiers, oleadas completadas, backlog repriorizado (storage antes que TCP/X11) e hitos de evolución.

**Qué es estable para probar en QEMU hoy:** [`STABLE.md`](STABLE.md) (canónico). Detalle EN: [`../ROADMAP.md`](../ROADMAP.md).

---

## Principios

| Principio | Significado |
|-----------|-------------|
| **Código sobre docs** | Estado desde fuente + smokes, no claims del README. |
| **Facades primero** | `includes/ir0/*`; sin `#include <drivers/...>` en portable. |
| **Un tier a la vez** | Slices verticales con prueba runnable. |
| **KTM = aliado dev** | Solo regresión/diagnóstico; no módulo de seguridad userspace. |
| **No romper userspace** | ABI de syscalls estable salvo ruptura versionada en mandocs. |
| **Sprints post-hito** | Tras oleada verde: sanitización arquitectónica antes del siguiente feature. |

---

## Escalera de madurez (2026-06-23)

| Tier | Objetivo | ~% | Prueba hoy |
|------|----------|-----|------------|
| **T0** | OS funcional + `debug_bins` | ~85% | `kernel-tests` 29/29, `arch-guard` |
| **T1** | Userspace POSIX (runit + musl + ash) | ~72–75% | `smoke-tier1`, smokes UNIX |
| **T2** | Gráficos fullscreen (Doom-class) | ~55% | fb0/evdev; GUI en STABLE |
| **T3** | Escritorio minimalista | ~15–20% | Solo planificación — WM fuera del kernel |

---

## Release 0.0.1 — baseline estable (cerrada)

Ver checklist completo en [`STABLE.md`](STABLE.md).

| Área | Estado |
|------|--------|
| Hardening H1–H6 | **Cerrado** |
| runit + BusyBox ash/applets | **Estable** |
| TinyCC | **Estable** |
| COW + lazy alloc | **Estable** |
| UDP POSIX mínimo | **Estable** |
| T2 fb/input/Doom GUI | **Estable para prueba** |

**Antes en desarrollo, ahora cerrado:** split syscalls, FASE en `fase_audit.c`, facades H3, budget `.text`, COW/lazy FASE40.

---

## Hitos completados (resumen)

### T1 / init / shell
- runit + ash interactivo, `sys_select`, manifest tier1.
- `smoke-tier1`, `roadmap-phase1-stability` (+ `smoke-mm-cow-lazy`).

### Red (UDP mínimo POSIX)
- socket/bind/sendto/recvfrom/connect — estable.
- TCP stream — **pospuesto** (P3).

### UNIX mínimo + musl
- ABI cred/señales, wait4 POSIX, grupos, setuid, CLONE_THREAD+futex.
- Smokes: multiuser-perms, musl-pthread, setuid-exec.

### KTM
- panic site, inventory, `make ktm-check`.

### MM — COW + lazy
- **Cerrado 0.0.1:** `smoke-mm-cow-lazy`.
- Mejoras futuras: COW 2 MiB, stack COW.

### Storage
- ATA, MINIX root, `/dev/hda` read, FAT16 read-only MVP.
- EXT2/AHCI — P1-storage.

### Hardening (2026-06-23)
- H1–H6 — ver [`HARDENING.md`](HARDENING.md).

---

## Post-0.0.1 — no “listo”

- FAT write, EXT2 ro, AHCI/NVMe
- TCP stream, AF_UNIX, X11/Wayland, WM (T3)
- SMP, módulos kernel, CFS backend
- PTY/job control; pthread_create vía musl en smoke (POSIX-1)

Backlog detallado: [`../ROADMAP.md`](../ROADMAP.md) secciones P1–P3.

---

## Gates CTR (mínimo)

```bash
make -s kernel-x64.bin
make -s kernel-tests
make -s arch-guard
make -s build-matrix-min
make -s -C tests/host run
make -s kernel-text-budget
make smoke-tier1    # T1 activo
make health         # batería extendida
```

---

## Documentos relacionados

| Doc | Tema |
|-----|------|
| [`STABLE.md`](STABLE.md) | Baseline 0.0.1 + QEMU UI |
| [`HARDENING.md`](HARDENING.md) | Sprints H1–H6 |
| [`../ROADMAP.md`](../ROADMAP.md) | Backlog EN completo |
| [`../SETUP.md`](../../SETUP.md) | Build e inject disk |
