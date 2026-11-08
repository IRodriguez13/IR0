# sysfs — `/sys/firmware/ir0/*`

> **Last verified:** 2026-11-08  
> **Source of truth:** `includes/ir0/boot_firmware_sysfs.h`, `kernel/boot_firmware_sysfs.c`,  
> `arch/*/sources/firmware_sysfs.c`, `fs/pseudo_fs_nodes.c`

IR0 exposes a best-effort firmware/boot snapshot under the sysfs pseudo-fs.

## Paths

| File | Content |
|------|---------|
| `protocol` | Boot protocol handed over by the ISA entry: `multiboot1` (x86_64 GRUB), `fdt` (ARM64 DTB), else `unknown`. |
| `bootloader` | Bootloader name when the protocol carries one (Multiboot flag bit 9; ARM64 reports `firmware-dtb-handoff`), else `unavailable`. |
| `cmdline` | Kernel command line from the boot snapshot (Multiboot cmdline or DTB `/chosen/bootargs`), else an empty line. |
| `memory_map` | Source of the usable-RAM inventory: `firmware` (Multiboot mmap or DTB memory nodes), `firmware-size` (Multiboot `mem_upper` only) or `builtin` (no firmware data). |
| `framebuffer` | Firmware framebuffer from the boot snapshot: `addr=0x… size=0x… width= height= pitch= bpp= type=` with a mode, `addr=0x… size=0x…` for a range without a mode (DTB node without a usable format), else `unavailable`. |
| `acpi_rsdp` | Physical address of the ACPI RSDP handed over in the boot snapshot (x86_64: legacy BIOS search, Multiboot 1 carries none), else `unavailable`. |
| `dtb` | Flattened devicetree range (`addr=0x… size=0x…`) when the firmware passed one, else `unavailable`. |
| `boot_detail` | Multi-line architecture-specific detail (Multiboot flags and mem fields on x86_64; FDT, CPU, IRQ and memory banks on ARM64). |

Common code (`kernel/boot_firmware_sysfs.c`) fills every node except
`bootloader` and `boot_detail` from the typed boot record
(`includes/ir0/boot_params.h`) and the neutral boot snapshot
(`includes/ir0/boot_memory.h`); the architecture backends add only those two.

All nodes are **read-only**, newline-terminated, and **bounded** in the implementation.

## Primary references

- Multiboot: <https://www.gnu.org/software/grub/manual/multiboot/>
- Devicetree (FDT): Linux `Documentation/devicetree/` (conceptual parity only).
