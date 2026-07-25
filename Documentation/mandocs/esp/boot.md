# Pipeline de arranque de IR0

| Campo | Valor |
|-------|-------|
| Versión | 0.2 |
| Fase IR0 | T0 / T1 producto |
| Estado | stable (producto: runit; debug shell opt-in) |
| Depende de | memory, drivers, vfs, process |
| Página man | IR0-boot (sección 7) |
| Fuentes principales | `arch/x86-64/asm/boot_x64.asm`, `arch/x86-64/sources/arch_early.c`, `kernel/main.c`, `fs/vfs.c`, `kernel/elf_loader.c`, `includes/ir0/klog_event.h`, `ktm/klog.c` |

## 1. Visión general

La ruta de arranque en x86-64 va desde una carga GRUB compatible con Multiboot
hasta tablas de páginas mínimas, `kmain`, puesta en marcha de drivers y VFS,
habilitación de syscalls/IRQ, y `kexecve("/sbin/init")` (runit). Los defconfigs
de producto ponen `CONFIG_KERNEL_DEBUG_SHELL=n`. El shell de depuración en
kernel solo queda disponible si se habilita explícitamente para contratos
legacy. Logging estructurado de arranque: [`KLOG.md`](../../KLOG.md) /
[`esp/KLOG.md`](../../esp/KLOG.md).

## 2. Arquitectura interna

| Etapa | Componente | Archivo |
|-------|------------|---------|
| Entrada del loader | Comprobación Multiboot, PAE, long mode | `arch/x86-64/asm/boot_x64.asm` |
| CPU temprana | GDT, TSS, SSE | `arch/x86-64/sources/arch_early.c` |
| Entrada del kernel | Orquestación + fases de boot | `kernel/main.c` (`kmain`) |
| Event core | `klog_record` + early clock | `ktm/klog.c`, `arch_early_clock_*` |
| Drivers | Bootstrap por etapas + probe events | `drivers/init_drv.c` |
| FS raíz | Init de tabla de montajes | `fs/vfs.c` (`vfs_init_root`) |
| Milestone bare-metal | `/etc/ir0-baremetal-booted` | `kernel/main.c` (post-VFS) |
| Userspace | Carga ELF + schedule | `kernel/elf_loader.c` (`kexecve`) |

**Paginación temprana (`boot_x64.asm`):** mapa de identidad 0–48 MiB con páginas
de 2 MiB; ventana opcional de framebuffer en `0xFD000000`. Pila en `0x8FF00`;
información Multiboot en `RDI` para `kmain`.

## 3. Flujo de datos

```
GRUB → boot_x64.asm
         → kmain(multiboot_info)
              → set_boot_params / early_init
              → heap_init (0x800000)
              → [CONFIG_ENABLE_VBE] video_backend_init_from_multiboot
              → console_backend_init
              → pmm_init (32–48 MiB)
              → logging_init + klog_promote_normal_ring
              → ir0_driver_registry_init + serial_init
              → fase EARLY_ARCH: ir0_boot_serial_ready() (banner BOOT)
              → líneas PLATFORM / HYPERVISOR
              → fase DRIVERS: init_all_drivers() + DRIVER_PROBE_RESULT*
              → vfs_init_root() + sentinel opcional FIRST_BAREMETAL_BOOT
              → process_init + ipc_init + clock_system_init (MONOTONIC)
              → syscall_init + syscalls_init
              → irq_init + boot_irq_unmask + sti
              → fase USERSPACE: kexecve("/sbin/init")  [default producto]
                OR [KERNEL_DEBUG_SHELL] start_init_process
              → sched_schedule_next → ring 3
```

## 4. Responsabilidades

- **boot_x64.asm:** solo transición de modo CPU; sin runtime C.
- **kmain:** init ordenado de subsistemas; no debe volver a userspace sin scheduler.
- **init_all_drivers:** registrar e init de stacks de hardware condicionados por Kconfig; emitir probe results.
- **vfs_init_root:** proporcionar un `/` usable antes de cualquier exec basado en fichero.
- **kexecve:** cargar ELF desde VFS, mapear segmentos, encolar proceso.

## 5. Límites del subsistema

- El ensamblador de arranque no debe llamar a VFS ni a kmalloc antes de `heap_init`.
- La init de drivers corre antes del montaje raíz VFS para que existan dispositivos de bloque para MINIX.
- La init de vídeo es opcional (`CONFIG_ENABLE_VBE`); fallback VGA vía `video_backend_init_fallback`.

## 6. Relaciones con otros subsistemas

| Vecino | Enlace |
|--------|--------|
| Memory | `heap_init`, `pmm_init` antes de la mayoría de subsistemas |
| Drivers | `init_all_drivers` antes del chequeo de bloque en `vfs_init_root` |
| VFS | Montaje raíz usa `CONFIG_ROOT_BLOCK_DEVICE`, `CONFIG_ROOT_FILESYSTEM` |
| Process | `process_init` antes del primer `kexecve` |
| Scheduler | Primera tarea de usuario entra vía `sched_schedule_next` |

## 7. Mapas visuales

```text
  [GRUB]──►[boot_x64]──►[kmain]──►[drivers]──►[VFS /]
                                      │              │
                                      ▼              ▼
                                 [block_dev]    [kexecve /sbin/init]
```

Fuente Mermaid: `Documentation/mandocs/diagrams/boot.mmd`

## 8. Invariantes importantes

1. La magia Multiboot debe ser `0x2BADB002` o el arranque se detiene con `"MN"` en VGA.
2. El pool PMM (`0x2000000`–`0x3000000`) no debe solaparse con el heap del kernel (`0x800000`–`0x2000000`).
3. `sti` solo corre tras init de IDT/PIC y tablas de syscall.
4. Si `sched_schedule_next` retorna tras el handoff de init, `kmain` entra en pánico.
5. No hay VA separada de kernel en higher-half; el mapa de identidad de arranque sirve a kernel y usuario temprano.

## 9. Consejos de depuración

Nuevos contribuidores: `make man TOPIC=onboarding`. Boot log opcional en el host:
`make run-bootlog` → `build/hostshare/ir0-boot.log` con `BOOT_LOG_HOSTSHARE=y`
y QEMU `-virtfs` (`BOOT_LOG_HOSTSHARE_OK` / `_SKIP`).

Tags serial: `[ARCH]`, COMP `BOOT` vía klog, `[DRIVERS]`, `DRIVER_SUMMARY_OK`,
`FIRST_BAREMETAL_BOOT*` (solo bare-metal), handoff userspace,
`[ts] [INFO] [FASE…] CLASSIFY ROOTFS_LAYOUT_OK`.

| Síntoma | Comprobar |
|---------|-----------|
| `"MN"` en pantalla | Desajuste de cabecera Multiboot |
| Fallo montaje raíz | `block_dev_is_present(CONFIG_ROOT_BLOCK_DEVICE)`; fallback tmpfs |
| GUI en blanco | `[BOOT] vbe_fail_reason=` (1=mb_null, 2=no_fb, 3=bad_dims, 4=map_fail) |
| Init no encontrado | Imagen MINIX sin `/sbin/init`; usar scripts inject |
| Sin `ir0-boot.log` en host | Hace falta `BOOT_LOG_HOSTSHARE=y` + `-virtfs`; `make help-bootlog` |

Build: `make kernel-x64.iso`; userspace: `make kernel-x64-userspace.iso`.

## 10. Hoja de ruta futura

- Arranque temprano ARM64 usa `ir0_boot_*` portable + `arm64_board` (`qemu-virt` /
  `rpi4` / stub `rpi5`). UART RPi4 min: `make smoke-arm64-rpi4-boot`. Hello sin CRT
  musl: fallback freestanding en `musl-aarch64-hello`.
- Arranque SMP/APIC-first no es primario (`CONFIG_ENABLE_SMP=0`).
- Mapa higher-half del kernel no implementado (solo mapa bajo de identidad).
- Arranque directo UEFI no está en el árbol (solo ruta GRUB Multiboot).
