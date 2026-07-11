# IR0 — Baseline estable (release 0.0.1)

> **Última verificación:** 2026-07-10  
> **Fuente de verdad:** smokes/`Makefile`, commits `f6c71e5` (KTM), `62cc512` (COW real),  
> [`../HARDENING.md`](../HARDENING.md), [`../ROADMAP.md`](../ROADMAP.md), gates CTR.

Checklist único de lo **estable para probar en QEMU** (serial y GTK), lo que **estaba en desarrollo** y quedó **cerrado en 0.0.1**, y lo que sigue siendo **trabajo futuro** ([`ROADMAP.md`](../ROADMAP.md) P1+).

Sign-off del mantenedor (2026-06-23): considerar **hecho para 0.0.1 final** salvo regresión en smokes.

**Nota de honestidad (2026-07-10):** textos STABLE anteriores afirmaban COW en fork mientras el
kernel aún hacía full-copy. El share-on-fork real + break en write fault está en `62cc512`
(`make smoke-mm-cow-lazy`). KTM es el plano de test canónico (`make ktm-run`,
`make ktm-userdev-run`); ver [`../ai_driven_dev/ktm.md`](../ai_driven_dev/ktm.md).

---

## Alcance release 0.0.1

| Área | Estado | Prueba principal |
|------|--------|------------------|
| **Hardening H1–H6** | **Cerrado** | [`HARDENING.md`](HARDENING.md); `make health` |
| **runit boot** | **Estable** | `make smoke-runit-boot` |
| **BusyBox ash + applets** | **Estable** | `make smoke-tier1`; opcional `smoke-fase58l-busybox-coreutils` |
| **TinyCC in-guest** | **Estable** | `make build-tcc-fase52` |
| **COW fork** | **Estable** | `make smoke-mm-cow-lazy` |
| **Lazy alloc** | **Estable** | mismo smoke |
| **Slice POSIX T1** | **Estable** | manifests tier1/musl; smokes cred/pthread/setuid |
| **Gráficos T2** | **Estable para prueba** | fb0, events0, mmap; stubs Doom |
| **Escritorio T3** | **Fuera de alcance** | WM fuera del árbol kernel |

---

## Antes en desarrollo — ahora estable (0.0.1)

### Hardening (H1–H6)

Ver tabla completa en [`../HARDENING.md`](../HARDENING.md). Resumen: split syscalls (86 L glue), FASE en `kernel/debug/fase_audit.c`, facades sin drivers en `includes/ir0/`, devfs read unificado, budget `.text` en `health`.

### Memoria (COW + lazy)

- Refcount PMM (`pmm_frame_get`/`put`), COW real en fork + break en `#PF` — **`make smoke-mm-cow-lazy`** (desde `62cc512`)
- Pendiente post-0.0.1: COW 2 MiB, file-backed, stack COW opcional
- Detalle: [`../mandocs/esp/mm.md`](../mandocs/esp/mm.md)
### Userspace

- runit, BusyBox (minimal + full opcional), ash interactivo en consola FB
- musl estático, TinyCC (`setup/tcc/`)

### Red

- UDP POSIX mínimo — **estable**; TCP stream — **no 0.0.1**

### Storage

- ATA, MINIX root, lectura `/dev/hda` — **estable**
- FAT16 read-only MVP on-disk — **estable acotado**; EXT2/AHCI — futuro

---

## Logros del roadmap — probables en QEMU con UI

| Tier | Capacidad | Smoke automático | GTK manual |
|------|-----------|------------------|------------|
| T0 | ktests + pseudo-FS | `make kernel-tests` | `make run` |
| T1 | runit + ash | `make smoke-tier1` | `make run-fase58e-ash-gui` |
| T1 | permisos multi-UID | `make smoke-multiuser-perms` | `su`/`id` en ash |
| T1 | COW + lazy | `make smoke-mm-cow-lazy` | — |
| T2 | fb0 / input | smokes legacy FASE54¹ | `run-fase58c-fbdev-gui`, Doom GUI |
| Dev | KTM + host | `make ktm-check`, `tests/host` | — |

¹ `IR0_LEGACY_SMOKE=1` para smokes históricos FASE54/55.

---

## QEMU — probar con interfaz gráfica

```bash
make defconfig
make run-fase58e-ash-gui
```

Construye disco MINIX temporal (irinit + BusyBox). **Sin** `IR0_LEGACY_SMOKE=1`.

- Ventana GTK; foco en teclado para escribir en el prompt `#`.
- Serial en la misma terminal.

Otros: `make run-irinit-interactive-gui`, `make run-fase55d-doomgeneric-gui` (IWAD), `make run` (dbgshell kernel).

Regresión headless:

```bash
make smoke-tier1
make smoke-mm-cow-lazy
make health
```

Ver [`../SETUP.md`](../../SETUP.md) y [`fase58e-ash-interactive-console.md`](fase58e-ash-interactive-console.md).

---

## No estable en 0.0.1

TCP stream, AF_UNIX, X11/Wayland, WM, SMP, módulos kernel, FAT16 write, EXT2, AHCI, PTY completo — ver [`ROADMAP.md`](ROADMAP.md).

---

## Documentos relacionados

- [`../STABLE.md`](../STABLE.md) — inglés canónico  
- [`ROADMAP.md`](ROADMAP.md) — backlog completo  
- [`HARDENING.md`](HARDENING.md) — sprints H1–H6  
