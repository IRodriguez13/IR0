# IR0 — Backlog post-0.0.1 (trabajo restante honesto)

> **Última verificación:** 2026-07-18  
> Espejo de [`../BACKLOG_REMAINING.md`](../BACKLOG_REMAINING.md) (inglés canónico).
> F8 MVP + ARCH + kill/#UD + piloto runit + **T3 checklist→runit + RTL8139 TX**:
> ver *Closed this wave (2026-07-18)* en el inglés.

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
10. F7b–F7g + Pack B–E (MM-root + INTERRUPT stubs + MEMORY probe) ~~hecho~~ — deuda = KERNEL_OBJS link ARM + musl aarch64 **BLOCKED**  
11. F8 … F13 — ver inglés; merge `master` solo con bundle TCC+Doom+userspace (STABLE)  
12. **F14 HAB** — AC remoto / domótica (userspace; VPN; ESP IR primero) — ver tablas HAB-* en inglés  
13. **F15 DESK** — **DESK-0…4 hechos** (soft fb); **SEP-1** contrato de árboles + rootfs esqueleto; DESK-5 JVM BLOCKED — ver inglés  
14. **F16 AST** — north star desktop tipo Astral (panel CDE, workspaces, audio, GL, irssi, JVM) — **AST-0** docs; resto Future — ver inglés  

## HAB / DESK / AST (resumen)

Mismo núcleo; demos HAB + DESK; **AST** = visión larga post-DESK. Detalle canónico en el inglés (`HAB-0…5`, `DESK-0…5`, `AST-0…9`).  
**DESK-0…4:** hechos en path **soft fb** (`make smoke-desk`, opcional / no release).  
**SEP-1:** [`IR0-desktop/Documentation/TREE_CONTRACT.md`](../../../IR0-desktop/Documentation/TREE_CONTRACT.md) + [`ARCH_DEBT_SEP.md`](../ARCH_DEBT_SEP.md).  
**AST-0:** north star documentado (panel, workspaces, term, IRC, audio, `glxgears`, editor portado, Minecraft/JVM, ISO).  
**No** reclamado hecho: ISO desktop ship, control de aire, IR nativo, ClassiCube upstream+GL, Minecraft/JVM, WM sobre TinyX, shell CDE/Astral-class.

## ARM64 — estado honesto

Bring-up freestanding en QEMU virt **sí**; port de OS **no**. Detalle en el inglés.  

## T3

WM solo userspace — fuera del árbol kernel.
Checklist sockets/IPC/fb: canónico **runit** (`make smoke-t3-prep`); stub `*-run` lab.
