# Drivers de IR0

| Campo | Valor |
|-------|-------|
| Versión | 0.1 |
| Fase IR0 | T0 |
| Estado | stable |
| Depende de | interrupts, memory |
| Página man | IR0-drivers (sección 7) |
| Fuentes principales | `kernel/driver_registry.c`, `drivers/driver_bootstrap.c`, `drivers/init_drv.c`, `interrupt/arch/pic.c`, `interrupt/arch/isr_handlers.c` |

## 1. Visión general

El soporte de hardware se organiza mediante un registro de drivers, un plan de
bootstrap por etapas e funciones init condicionadas por Kconfig bajo `drivers/`.
El acceso visible para usuario suele ser vía nodos devfs o fachadas en
`includes/ir0/*`, no llamadas directas a drivers desde código portable del kernel.

## 2. Arquitectura interna

| Componente | Rol |
|------------|-----|
| `driver_registry.c` | Lista enlazada, estados UNREGISTERED→ACTIVE, máx. 128 |
| `driver_bootstrap.c` | Hasta 32 registros por etapas, `driver_bootstrap_run_all` |
| `init_drv.c` | `init_all_drivers()` — INPUT→PLATFORM→STORAGE→AUDIO→NETWORK |
| `resource_registry` | Registro IRQ/IOPORT desde drivers |
| `multilang_drivers.c` | Drivers ejemplo Rust/C++ cuando `KERNEL_ENABLE_EXAMPLE_DRIVERS` |

**Etapas bootstrap (`init_drv.c`):**

```text
  INPUT     ps2_controller, ps2_keyboard, ps2_mouse
  PLATFORM  pc_speaker, usb_host
  STORAGE   ata_core, ata_block, ahci (DMA EXT + NCQ FPDMA si CAP.SNCQ)
  AUDIO     sound_stack (SB16, AdLib, DMA)
  NETWORK   network_stack, bluetooth_stack (condicionado Kconfig)
```

## 3. Flujo de datos

```text
  kmain → ir0_driver_registry_init()
       → init_all_drivers()
            → driver_bootstrap_run_all() por etapa
                 → init/register ops de cada driver
       → block_dev listo para MINIX
       → devfs_register_node() para /dev/*

  IRQ n (32–47 PIC)
       → isr_handlers.c
            → pic_send_eoi64
            → manejador dispositivo (p. ej. keyboard_handler64)
                 → keyboard_poll_ps2 + stdin_wake_check
```

## 4. Responsabilidades

- Los drivers registran capacidades; devfs/procfs exponen rutas user.
- Init fallida incrementa contador de fallos bootstrap pero la ejecución continúa.
- Drivers de almacenamiento proporcionan fachada `block_dev` para sistemas de ficheros.

## 5. Límites del subsistema

- `kernel/` no debe `#include <drivers/...>` — usar `includes/ir0/*`.
- `drivers/` no debe `#include <arch/...>` — usar `ir0/arch_port.h`.
- Headers Bluetooth solo bajo `drivers/bluetooth/`.

## 6. Relaciones con otros subsistemas

| Vecino | Interacción |
|--------|-------------|
| devfs | `/dev/disk`, `/dev/fb0`, `/dev/net`, … |
| procfs | `/proc/drivers`, interrupts, blockdevices |
| VFS | minix vía block_dev |
| Timer | PIT en `clock_system.c`; código LAPIC presente |
| Net | stack `net/` + rtl8139 cuando está habilitado |

## 7. Mapas visuales

```text
  init_all_drivers
        │
   ┌────┴────┬─────────┬─────────┬────────┐
   ▼         ▼         ▼         ▼        ▼
  PS/2     ATA/VBE   SB16     RTL8139   USB
   │         │         │         │        │
   └────┬────┴────┬────┴────┬────┴────────┘
        ▼         ▼         ▼
     devfs    block_dev   /proc
```

Ruta IRQ:

```text
  IRQ hardware ──► IDT ──► PIC ISR ──► ISR driver ──► wake/poll
```

## 8. Invariantes importantes

1. `init_all_drivers` es one-shot (`g_bootstrap_done`).
2. Registry máx. 128 drivers; bootstrap máx. 32 entradas.
3. IRQs PIC 32–47 en ruta primaria x86-64.
4. Drivers multilang ejemplo apagados salvo Kconfig habilitado.

## 9. Consejos de depuración

- `/proc/drivers` — instantánea del registry en tiempo de lectura.
- `/proc/interrupts` — contadores IRQ.
- Tags serial `[DRIVERS]` durante `kmain`.
- `CONFIG_ENABLE_NETWORKING=0` por defecto — el stack net puede compilar pero estar deshabilitado.

## 10. Hoja de ruta futura

- AHCI NCQ (FPDMA) aterrizado 2026-07-11 — tags `AHCI_NCQ_OK` / `AHCI_NCQ_UNSUPPORTED`
  (`smoke-ahci-read`); soft-off ACPI con FADT + DSDT `_S5_` (`ACPI_S5_OK`).
- NVMe MVP — Future F6 (`BACKLOG_REMAINING.md`).
- APIC/LAPIC como demux IRQ primario — parcial (existe código timer LAPIC).
- Hotplug almacenamiento USB — solo init host en bootstrap hoy.
- Modelo unload/módulo de driver — **no implementado**.
- Locks driver seguros SMP — suposición single-CPU.

Legado: `Documentation/DRIVERS.md`, `Documentation/INTERRUPTS.md`.
