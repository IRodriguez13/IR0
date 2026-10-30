# Makefile IR0 — orquestador central

Documento primario en ingles: [`../MAKEFILE.md`](../MAKEFILE.md).

Resume el rol del `Makefile` raiz: flujo de `.config`, taxonomia de targets
(build, run, tests/gates, documentacion) y puntos de cableado obligatorios
(Kconfig, defconfig, Makefile, menuconfig).

Userspace de producto: hermano **ISD** (construido fuera de este arbol); ver
[`USERSPACE.md`](USERSPACE.md). No hay `debug_bins/` in-tree.

Para la lista completa de targets ejecutar `make help`.
