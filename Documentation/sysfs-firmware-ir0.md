# sysfs — `/sys/firmware/ir0/*`

> **Last verified:** 2026-11-06  
> **Source of truth:** `includes/ir0/boot_firmware_sysfs.h`, `kernel/boot_firmware_sysfs.c`,  
> `arch/*/sources/boot_firmware_sysfs_arch.c`, `fs/pseudo_fs_nodes.c`

IR0 exposes a best-effort firmware/boot snapshot under the sysfs pseudo-fs.

## Paths

| File | Content |
|------|---------|
| `protocol` | Boot protocol label (`multiboot1` on x86_64 with Multiboot info; `fdt` or `dtb_invalid` on ARM64). |
| `bootloader` | Bootloader name when provided (Multiboot flag bit 9), else `unavailable`. |
| `cmdline` | Kernel command line when present (Multiboot cmdline field), else empty line only. |
| `framebuffer` | Framebuffer summary on Multiboot+FB; on ARM64 early console MMIO when known, else `unavailable`. |
| `boot_detail` | Multi-line architecture-specific detail (flags, memory banks on ARM64, Multiboot mem fields on x86). |

All nodes are **read-only**, newline-terminated, and **bounded** in the implementation.

## Primary references

- Multiboot: <https://www.gnu.org/software/grub/manual/multiboot/>
- Devicetree (FDT): Linux `Documentation/devicetree/` (conceptual parity only).
