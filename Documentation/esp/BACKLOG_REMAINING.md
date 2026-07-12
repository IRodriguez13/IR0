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
7. ~~F7.1 ARM64 early MM~~ **hecho** — `smoke-arm64-mmu`  
8. ~~F7.2 ARM64 VBAR/SVC~~ **hecho** — `smoke-arm64-vbar`  
9. ~~F7.3 ARM64 EL0 + PSCI~~ **hecho** — `smoke-arm64-el0` / `smoke-arm64`  
10. F7b.1 slice compile+link ~~hecho~~ — `smoke-arm64-slice`; F7b.2+ PL011/paging — ver inglés  
11. F8 … F12 — ver inglés

## ARM64 — estado honesto

Bring-up freestanding en QEMU virt **sí**; port de OS **no**. Detalle en el inglés.  

## T3

WM solo userspace — fuera del árbol kernel.
