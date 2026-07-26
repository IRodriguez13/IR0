# Makefile IR0 — orquestador central

Documento primario en ingles: [`../MAKEFILE.md`](../MAKEFILE.md).

Resume el rol del `Makefile` raiz: flujo de `.config`, taxonomia de targets
(build, run, smoke, validacion, documentacion) y puntos de cableado obligatorios
(Kconfig, defconfig, Makefile, menuconfig).

Userspace de producto: hermano **IR0-userspace** (`IR0_USERSPACE_ROOT`); ver
[`USERSPACE.md`](USERSPACE.md). No hay `debug_bins/` in-tree.

Para la lista completa de targets ejecutar `make help`.
