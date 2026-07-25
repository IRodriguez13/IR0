# IR0 Debug Shell (debug_bins) — harness legado

> **Última verificación:** 2026-07-24
> **Fuente de verdad:** ver [`../en/debug-bins.md`](../en/debug-bins.md)

Producto: `CONFIG_KERNEL_DEBUG_SHELL=n` / `CONFIG_DEBUG_BINS=n`. Exploración T0 =
ash + `/proc/kmsg`. El árbol `debug_bins/` se conserva para migrar contratos;
no borrar hasta que ktest/userspace los cubran.
