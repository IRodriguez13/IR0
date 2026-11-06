# sysfs — `/sys/firmware/ir0/*`

> **Última verificación:** 2026-11-06  
> **Fuente de verdad:** `includes/ir0/boot_firmware_sysfs.h`, `kernel/boot_firmware_sysfs.c`,  
> `arch/*/sources/boot_firmware_sysfs_arch.c`, `fs/pseudo_fs_nodes.c`

IR0 expone una instantánea best-effort del firmware/arranque bajo el pseudo-fs sysfs.

## Rutas

| Archivo | Contenido |
|---------|-----------|
| `protocol` | Etiqueta de protocolo (`multiboot1` en x86_64 con info Multiboot; `fdt` o `dtb_invalid` en ARM64). |
| `bootloader` | Nombre del cargador si viene (bit 9 Multiboot), si no `unavailable`. |
| `cmdline` | Línea de comando del kernel si existe (campo cmdline Multiboot), si no solo salto de línea. |
| `framebuffer` | Resumen con Multiboot+FB (`addr=0x` física de 64 bits); en ARM64 MMIO de consola temprana si se conoce, si no `unavailable`. |
| `boot_detail` | Varias líneas con detalle por ISA (flags, bancos de memoria en ARM64, campos mem en x86). |

Todos los nodos son de **solo lectura**, terminan en newline y están **acotados** en la implementación.

## Referencias

- Multiboot: <https://www.gnu.org/software/grub/manual/multiboot/>
- Devicetree (FDT): Linux `Documentation/devicetree/` (paridad conceptual solamente).
