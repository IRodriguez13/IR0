# Makefile IR0 — orquestador central

Documento primario en ingles: [`../MAKEFILE.md`](../MAKEFILE.md).

Resume el rol del `Makefile` raiz: flujo de `.config`, taxonomia de targets
(build, run, tests/gates, documentacion) y puntos de cableado obligatorios
(Kconfig, defconfig, Makefile, menuconfig).

Userspace de producto: hermano **ISD** (construido fuera de este arbol); ver
[`USERSPACE.md`](USERSPACE.md). No hay `debug_bins/` in-tree.

Los goals cuyo nombre empieza por `kernel-arm64`, `smoke-arm64` o `arm64-`
(p. ej. `make kernel-arm64.bin`) **fuerzan `ARCH=arm64`** para cargar los
fragmentos ARM64 aunque `.config` predetermine x86-64.

Para la lista completa de targets ejecutar `make help`.
