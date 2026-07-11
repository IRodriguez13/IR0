# IR0 — Backlog post-0.0.1 (trabajo restante honesto)

> **Última verificación:** 2026-07-10  
> Espejo de [`../BACKLOG_REMAINING.md`](../BACKLOG_REMAINING.md) (inglés canónico).

## Closed

Ver tabla en el archivo inglés. Incluye F1–F7, KTM v1, `/dev/ktm`, retiro de `[FASE`,
scenarios P0/P1 MM en `ktm-run` (pass=8: `mm.vma` / `mm.page_tables` / `mm.steady_state`),
applets BusyBox `halt`/`poweroff`/`reboot`, COW real en fork, reparent sin CRITICAL,
rename `ir0_mm_*`, ARCH-4 `DEBUG_BOOT`.

## Open

| Ítem | Prueba |
|------|--------|
| Paridad FASE→KTM restante (PARTIAL/GAP) | [`KTM_FASE_PARITY.md`](KTM_FASE_PARITY.md) — shell/fb/OOM/drain |

## Siguiente — deuda + sprints

1. ARCH-3 / ARCH-2 / PERF-1 / POSIX-2 (ver inglés)  
2. KTM-P2: cases userdev shell/fb; COW A–F opcional

## Future / P2

NVMe/NCQ, W10b, userspace ARM64, TCP Internet, kexec/suspend, Rust ABI, SMP/CFS,
T3 WM userspace — oleadas dedicadas.
