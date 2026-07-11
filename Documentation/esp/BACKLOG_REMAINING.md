# IR0 — Backlog post-0.0.1 (trabajo restante honesto)

> **Última verificación:** 2026-07-10  
> Espejo de [`../BACKLOG_REMAINING.md`](../BACKLOG_REMAINING.md) (inglés canónico).

## Closed

Ver tabla en el archivo inglés. Incluye power MVP + ACPI PM1a QEMU, stubs
kexec/suspend (`ENOSYS`), POSIX-2 `setsid`/`setpgid`, KTM-P1 MM (pass=8),
applets BusyBox halt/poweroff/reboot, COW real, ARCH-4.

## Open

| Ítem | Prueba |
|------|--------|
| Paridad FASE→KTM restante (PARTIAL/GAP) | [`KTM_FASE_PARITY.md`](KTM_FASE_PARITY.md) — shell/fb/OOM/drain |
| POSIX-2 residual | SIGHUP al cerrar TTY controladora |

## Siguiente

1. KTM-P2 userdev shell/fb; SIGHUP/TTY  
2. Future: kexec real, S3–S4, AML `_S5`, NVMe, SMP, T3 userspace

## Future / P2

NVMe/NCQ, W10b, userspace ARM64, TCP Internet, kexec real / suspend S3–S4,
Rust ABI, SMP/CFS, T3 WM userspace — oleadas dedicadas.
