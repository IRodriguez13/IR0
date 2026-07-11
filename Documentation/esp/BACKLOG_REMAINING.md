# IR0 — Backlog post-0.0.1 (trabajo restante honesto)

> **Última verificación:** 2026-07-11  
> Espejo de [`../BACKLOG_REMAINING.md`](../BACKLOG_REMAINING.md) (inglés canónico).

## Closed

Ver tabla en el archivo inglés. Open residual KTM/HOST cerrado (pass=16,
COW A–F, reclaim 41, ACPI FADT map on-demand, F2–F5). **Open vacío.**

## Open

_(vacío)_

## Future (simple → complejo)

1. ~~F1 ACPI map seguro~~ **hecho**  
2. ~~F2 AHCI NCQ~~ **hecho** — `AHCI_NCQ_OK` / `AHCI_NCQ_UNSUPPORTED`  
3. ~~F3 AML `_S5` SLP_TYP~~ **hecho** — `ACPI_S5_OK`  
4. ~~F4 kexec mínimo~~ **hecho** — stub + `kexec_load` MVP (`smoke-kexec-load`)  
5. ~~F5 Suspend / S3~~ **hecho** — `_S3_` + soft resume (`smoke-reboot-s3`)   
6. ~~F6 NVMe~~ **hecho** — `smoke-nvme-read`  
7. F7 ARM64 MM … F12 — ver inglés  

## T3

WM solo userspace — fuera del árbol kernel.
