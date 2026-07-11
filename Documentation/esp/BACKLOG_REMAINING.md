# IR0 — Backlog post-0.0.1 (trabajo restante honesto)

> **Última verificación:** 2026-07-10  
> Espejo de [`../BACKLOG_REMAINING.md`](../BACKLOG_REMAINING.md) (inglés canónico).

## Closed

Ver tabla en el archivo inglés. Incluye power MVP, POSIX-2 setsid/SIGHUP,
KTM boot pass=15 (drain/fb/events/open_flags + P2 previo), userdev
`fork_wait_signal` + `cow_touch`, PERF gettid, COW real, ARCH-4.

## Open

| Ítem | Prueba |
|------|--------|
| Residual HOST/GAP | [`KTM_FASE_PARITY.md`](KTM_FASE_PARITY.md) — TCC/Doom (52/55), reclaim 41, A–F COW smoke |

## Siguiente

1. Future: kexec real, S3–S4, AML `_S5`, ACPI map >48 MiB, NVMe, SMP, T3  
2. Opcional HOST: retirar `smoke-mm-cow-lazy` solo si A–F entra en userdev  

## Future / P2

NVMe/NCQ, W10b, userspace ARM64, TCP Internet, kexec real / suspend S3–S4,
Rust ABI, SMP/CFS, T3 WM userspace — oleadas dedicadas.
