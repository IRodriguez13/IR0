# Soporte multi-arquitectura de IR0

| Campo | Valor |
|-------|-------|
| Versión | 0.4 |
| Fase IR0 | T0 |
| Estado | stable |
| Depende de | boot, syscalls, scheduler |
| Página man | IR0-multi-arch (sección 7) |
| Fuentes principales | Ver versión inglesa `Documentation/mandocs/en/multi-arch.md` (fachadas `irq`/`arch_switch`/`arch_mm`/`arch_signal`) |
| Última verificación | 2026-11-06 |

## 1. Visión general

IR0 separa código portable del kernel de backends de arquitectura bajo `arch/`.
**x86-64** es el objetivo de producción (ISO, userspace musl, ruta syscall
completa). **arm64** tiene kernel de producto portable: `kernel-arm64-product.bin`
(y `kernel-arm64.bin`, copia tras `arm64-common-core-compile` +
`arm64-provider-audit`) enlaza el mismo **`$(ALL_OBJS)`** que x86. Las imágenes
`-min` por placa (`kernel-arm64-<board>-min.bin`, `scripts/make/arm64-board.mk`)
mantienen un camino **freestanding** para direcciones de carga del firmware.
El hueco abierto es **musl aarch64 + empaquetado rootfs ISD**, no el paso de link
del kernel.

La secuencia histórica F7 en QEMU virt (MMU → VBAR → EL0) sigue siendo contexto
útil abajo; el arranque de producto hoy usa la cadena compartida `ir0_kernel_boot`
con `IR0_FREESTANDING_BOOT` **apagado** en `kernel-arm64-product.bin`.

CFLAGS de boot incluyen `-mgeneral-regs-only` para no atrapar NEON en EL1 temprano.

## 2. Arquitectura interna

| Capa | Ubicación | Rol |
|------|-----------|-----|
| Fachada portable | `arch_port.h`, `irq.h`, `context.h` | CPU, IRQ, `switch_to` |
| Backends ISA | `arch/*/sources/arch_*.c` | switch, mm, signal, syscall frame, irq_init |
| sigcontext | `sigcontext_x86_64.h` / `sigcontext_arm64.h` | uapi Linux en ambas ISA |
| Interfaz común | `arch/*/sources/interface.c` | Despacho cross-arch |
| x86-64 | `arch/x86-64/` + `arch/x86-64/sources/interrupt/` | IDT/PIC impl + userspace |
| arm64 | `arch/arm64/sources/` | VBAR+GIC; `arch_switch`/`arch_mm` TTBR0; link producto `kernel-arm64-product.bin` = **`$(ALL_OBJS)`**; `-min` = freestanding |
| Context switch | `switch_to` → backend privado por ISA | dispatcher en `sched/switch/context_switch.c` |
| Config | `setup/Kconfig`, `ARCH=` en Makefile | `ARCH_OBJS_ARM64` incluye `switch_early.S` |

**Tags guard (`architecture_guard.py`):** árboles portables no deben incluir
`<arch/...>` o `<drivers/...>` directamente; usar fachadas.

## 3. Flujo de datos

**Ruta syscall x86-64:**

```text
  musl → insn syscall → syscall_insn_entry_64.asm
       → syscall_dispatch
       → handler → sysret

  userspace → insn syscall (preferida) / int 0x80 → dispatch
```

**Context switch (producto x86):**

```text
  sched_schedule_next → first_switch_to(next)   # primera entrada
                     → switch_to(prev, next)    # posteriores
```

**arm64 secuencia F7 histórica (oleadas; código borrado el 2026-11-07):**

```text
  _start → BOOT_OK → MMU_OK → VBAR → tags SVC / EL0
        → F7h switch_early (callee-saved)
        → F7i TTBR dual (l1_table / l1_table_b) → ARM64_TTBR_*_OK
        → F7j process_early: arm64_cpu_switch_mm A↔B
             → ARM64_PROCESS_SWITCH_OK / ARM64_PROCESS_TTBR_OK
        → PSCI SYSTEM_OFF
```

## 4. Responsabilidades

- `arch/` implementa hooks declarados en `arch_portable.h` / `arch_port.h`.
- Código portable selecciona comportamiento vía `CONFIG_*` y fachadas, no `#ifdef` en `fs/`.
- Makefile condiciona listas de objetos por `ARCH=x86-64|arm64`.
- Imágenes `-min` / smokes freestanding viejos no equivalen a “musl producto completo en ARM64”.

## 5. Límites del subsistema

| Regla | Alcance |
|-------|---------|
| `fs-no-direct-arch` | `fs/` → solo `ir0/arch_port.h` |
| `mm-net-no-arch-include` | `mm/`, `net/` → fachadas |
| `kernel-use-arch-port-facade` | sin `arch_portable.h` directo en kernel |
| `drivers-no-arch` | drivers → `ir0/arch_port.h` |

## 6. Relaciones con otros subsistemas

| Vecino | Interacción |
|--------|-------------|
| Scheduler | switch por arch; primera entrada vía `first_switch_to` |
| Syscalls | entry ASM / stub por arch |
| Interrupts | PIC (x86) vs scaffold GIC (arm64) |
| Drivers | port I/O vía fachada; bloques teclado solo x86 |
| VFS/procfs | info CPU vía `arch_port` |

## 7. Mapas visuales

```text
  portable kernel (fs/mm/net/kernel)
           │
           ▼
    includes/ir0/arch_port.h
           │
     ┌─────┴─────┐
     ▼           ▼
  x86-64      arm64
  (ISO completo)  (producto ALL_OBJS + -min + probes)
     │           │
  boot/syscall  ir0_kernel_boot / board_boot_min -min
  switch_x64    switch + MMU / GIC
```

Checklist de porting:

```text
  nueva arch → linker.ld + boot entry
             → early_init
             → entry syscall + ABI dispatch
             → switch_context_* + first_switch_to
             → fault/MMU setup
             → revisión de exenciones architecture_guard
```

## 8. Invariantes importantes

1. Layout de `task_t` fijo para ASM x86-64 — cambiar offsets rompe el switch.
2. `ARCH_SUPPORTS_APIC` es 1 en config x86-64, 0 en arm64.
3. Ficheros scaffold listados en el guard deben existir para builds matrix arm64.
4. Smokes musl/ISO de producción apuntan solo a x86-64 hoy.
5. `arm64_cpu_switch_mm`: activa `next_ttbr` si es no-cero y distinto del TTBR0 actual.
6. **Contrato serial de boot ISA-agnóstico:** tras init UART, `ir0_boot_serial_ready()`
   emite primero el banner `[BOOT] IR0 kernel <ver>`; detalle ISA en COMP
   `ARCH`, tags de etapa en COMP `SMOKE` (`includes/ir0/boot_log.h`).
7. **Board ARM64 en compile-time** (`includes/ir0/arm64_board.h`, `ARM64_BOARD=` /
   Kconfig): `qemu-virt` (PL011 `0x09000000`), `rpi4` (PL011 `0xfe201000`, QEMU
   `raspi4b`), `rpi5` (UART stub — sin RP1 aún). Sin parseo DTB todavía.

## 9. Consejos de depuración

- `make build-matrix-min` — compila variantes arch según matrix.
- `make arch-guard` — violaciones de fachada antes de merge.
- `get_arch_name()` / `/proc/cpuinfo` para cadena ISA en runtime.
- Build producto arm64: `make ARCH=arm64 kernel-arm64.bin` → `kernel-arm64-product.bin`
  (**`$(ALL_OBJS)`**) + `kernel-arm64.img` para QEMU `-kernel`.
- Check serial QEMU: `make smoke-arm64-hypervisor-log` (exige `qemu-system-aarch64`;
  **SKIP** si falta).
- Compile portable: `make arm64-portable-compile` (objs curados — **no** es un link).
- Probe: `make arm64-all-objs-probe` (MEMORY + muestra KERNEL compile-only).
- Boards: `make smoke-arm64-rpi4-boot` (QEMU `raspi4b` o **SKIP** si no hay máquina);
  `make arm64-rpi5-compile` (solo strings stub). Ver `scripts/make/arm64-board.mk`.

## 10. Hoja de ruta futura

- `fork`/`exec` / `sched/rr` con paridad Linux plena — producto ya usa `syscall_dispatch`; falta rootfs ISD AArch64 y endurecer smokes BusyBox.
- **musl aarch64 + rootfs ISD AArch64** — hueco de toolchain/packaging; el kernel ya enlaza **`$(ALL_OBJS)`** (`kernel-arm64-product.bin`).
- GIC / timers — endurecer política según avance el port.
- Raspberry Pi 4: GIC-400, SD, mailbox, DTB (más allá del UART min).
- Raspberry Pi 5: UART RP1 + bring-up real (hoy solo stub de board id).
- Eliminar clusters `#ifdef` solo x86 en keyboard/console.
- Boot UEFI en x86 — solo GRUB Multiboot hoy.
- RISC-V / x86-32 — **no en el árbol**.

Legado: `Documentation/DECOUPLING.md`, `arch/README.md`.
