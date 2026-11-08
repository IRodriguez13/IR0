# sysfs — `/sys/firmware/ir0/*`

> **Última verificación:** 2026-11-08  
> **Fuente de verdad:** `includes/ir0/boot_firmware_sysfs.h`, `kernel/boot_firmware_sysfs.c`,  
> `arch/*/sources/firmware_sysfs.c`, `fs/pseudo_fs_nodes.c`

IR0 expone una instantánea best-effort del firmware/arranque bajo el pseudo-fs sysfs.

## Rutas

| Archivo | Contenido |
|---------|-----------|
| `protocol` | Protocolo de arranque que entrega la entrada de la ISA: `multiboot1` (GRUB en x86_64), `fdt` (DTB en ARM64), si no `unknown`. |
| `bootloader` | Nombre del cargador si el protocolo lo trae (bit 9 Multiboot; ARM64 informa `firmware-dtb-handoff`), si no `unavailable`. |
| `cmdline` | Línea de comando del kernel tomada de la instantánea de arranque (cmdline Multiboot o `/chosen/bootargs` del DTB), si no solo salto de línea. |
| `memory_map` | Origen del inventario de RAM usable: `firmware` (mmap Multiboot o nodos memory del DTB), `firmware-size` (solo `mem_upper` Multiboot) o `builtin` (sin datos del firmware). |
| `framebuffer` | Framebuffer del firmware según la instantánea: `addr=0x… size=0x… width= height= pitch= bpp= type=` con modo, `addr=0x… size=0x…` para un rango sin modo, si no `unavailable`. |
| `acpi_rsdp` | Dirección física del RSDP ACPI de la instantánea (x86_64: búsqueda BIOS heredada, Multiboot 1 no lo trae), si no `unavailable`. |
| `dtb` | Rango del devicetree (`addr=0x… size=0x…`) si el firmware lo pasó, si no `unavailable`. |
| `boot_detail` | Varias líneas con detalle por ISA (flags y campos mem Multiboot en x86_64; FDT, CPU, IRQ y bancos de memoria en ARM64). |

El código común (`kernel/boot_firmware_sysfs.c`) llena todos los nodos salvo
`bootloader` y `boot_detail` desde el registro de arranque tipado
(`includes/ir0/boot_params.h`) y la instantánea neutral
(`includes/ir0/boot_memory.h`); los backends de arquitectura agregan solo esos dos.

Todos los nodos son de **solo lectura**, terminan en newline y están **acotados** en la implementación.

## Referencias

- Multiboot: <https://www.gnu.org/software/grub/manual/multiboot/>
- Devicetree (FDT): Linux `Documentation/devicetree/` (paridad conceptual solamente).
