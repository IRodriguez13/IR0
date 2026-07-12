# Soporte multi-arquitectura de IR0

| Campo | Valor |
|-------|-------|
| Versión | 0.1 |
| Fase IR0 | T0 |
| Estado | stable |
| Depende de | boot, syscalls, scheduler |
| Página man | IR0-multi-arch (sección 7) |
| Fuentes principales | `arch/common/arch_interface.c`, `arch/x86-64/`, `arch/arm64/`, `scripts/architecture_guard.py`, `includes/ir0/arch_port.h` |

## 1. Visión general

IR0 separa código portable del kernel de backends de arquitectura bajo `arch/`.
**x86-64** es el objetivo de producción (ISO, userspace musl, ruta syscall
completa). **arm64** tiene MMU (F7.1), VBAR/SVC (F7.2), EL0+PSCI (F7.3) en la
imagen freestanding; link completo / musl / context switch siguen pendientes.

## 2. Arquitectura interna

| Capa | Ubicación | Rol |
|------|-----------|-----|
| Fachada portable | `includes/ir0/arch_port.h` | Consultas CPU, habilitación IRQ, fachada port I/O |
| Interfaz común | `arch/common/arch_interface.c` | Despacho cross-arch |
| x86-64 | `arch/x86-64/` | boot, IDT, PIC, modo user, syscalls |
| arm64 | `arch/arm64/sources/` | boot_stub, mmu_early, vectors/exc_early (F7.2), scaffold |
| Context switch | `sched/switch/switch_x64.asm`, `switch_arm64.c` | por ISA |
| Config | `setup/Kconfig`, `ARCH=` en Makefile | selección de objetos |

**Tags guard (`architecture_guard.py`):** árboles portables no deben incluir
`<arch/...>` o `<drivers/...>` directamente; usar fachadas.

## 3. Flujo de datos

**Ruta syscall x86-64:**

```text
  musl → insn syscall → syscall_insn_entry_64.asm
       → syscall_dispatch (kernel/syscalls.c)
       → handler → sysret

  debug_bins → int 0x80 → syscall_entry_64.asm → dispatch
```

**Context switch:**

```text
  sched_schedule_next → arch_context_switch.c
       → switch_context_x64 (ASM) o arch_switch_to_user (primera entrada)
```

**arm64 (actual):**

```text
  _start → BOOT_OK → MMU_OK → VBAR → EL1 svc → SVC_RET_OK
        → EL0_DROP → EL0 svc → EL0_SVC_OK → EL0_RET_OK → PSCI_OFF
  switch_arm64.c → stub vacío (sched completo fuera de la imagen boot)
```

## 4. Responsabilidades

- `arch/` implementa hooks declarados en `arch_portable.h` / `arch_port.h`.
- Código portable selecciona comportamiento vía `CONFIG_*` y fachadas, no `#ifdef` en `fs/`.
- Makefile condiciona listas de objetos por `ARCH=x86-64|arm64`.

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
| Scheduler | implementación switch por arch |
| Syscalls | ASM entrada por arch / stub |
| Interrupts | PIC (x86) vs scaffold GIC (arm64) |
| Drivers | port I/O vía fachada; bloques teclado solo x86 |
| VFS/procfs | info CPU vía `arch_port` |

## 7. Mapas visuales

```text
  kernel portable (fs/mm/net/kernel)
           │
           ▼
    includes/ir0/arch_port.h
           │
     ┌─────┴─────┐
     ▼           ▼
  x86-64      arm64
  (completo)  (scaffold)
     │           │
  boot/syscall  boot_stub
  switch_x64    switch stub
```

Checklist de porting:

```text
  nueva arch → linker.ld + entrada boot
          → arch_early_init
          → entrada syscall + ABI dispatch
          → switch_context_* 
          → setup fault/MMU
          → revisión exenciones architecture_guard
```

## 8. Invariantes importantes

1. Layout `task_t` fijo para ASM x86-64 — cambiar offsets rompe switch.
2. `ARCH_SUPPORTS_APIC` es 1 en config x86-64, 0 en arm64.
3. Ficheros scaffold listados en guard deben existir para builds matrix arm64.
4. Smokes de producción y toolchain musl apuntan solo a x86-64 hoy.

## 9. Consejos de depuración

- `make build-matrix-min` — compila variantes arch según matrix.
- `make arch-guard` — violaciones de fachada antes de merge.
- `arch_get_name()` / `/proc/cpuinfo` para cadena ISA en runtime.
- Boot arm64: `make smoke-arm64` (boot+mmu+vbar+el0 en QEMU virt).
- Link scaffold arm64: `make ARCH=arm64 kernel-arm64.bin` (sin ruta ISO userspace completa).

## 10. Hoja de ruta futura

- Link completo `ALL_OBJS` arm64 + musl userspace / context switch.
- Eliminar clusters `#ifdef` solo x86 en keyboard/console para portabilidad real.
- Boot UEFI en x86 — solo GRUB Multiboot hoy.
- RISC-V / x86-32 — **no en el árbol** (`arch/README.md` puede estar obsoleto).

Legado: `Documentation/DECOUPLING.md`, `arch/README.md`.
