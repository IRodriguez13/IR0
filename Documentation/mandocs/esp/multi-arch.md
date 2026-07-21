# Soporte multi-arquitectura de IR0

| Campo | Valor |
|-------|-------|
| Versión | 0.2 |
| Fase IR0 | T0 |
| Estado | stable |
| Depende de | boot, syscalls, scheduler |
| Página man | IR0-multi-arch (sección 7) |
| Fuentes principales | `arch/common/arch_interface.c`, `arch/x86-64/`, `arch/arm64/sources/` (`boot_stub.c`, `mmu_early.c`, `switch_early.S`, `process_early.c`, `first_switch.c`), `sched/switch/switch_arm64.c`, `scripts/architecture_guard.py`, `includes/ir0/arch_port.h` |

## 1. Visión general

IR0 separa código portable del kernel de backends de arquitectura bajo `arch/`.
**x86-64** es el objetivo de producción (ISO, userspace musl, ruta syscall
completa). **arm64** freestanding en QEMU virt cubre MMU identity map, VBAR/SVC
EL1, drop EL0, scaffold GIC/timer, context switch temprano (**F7h**), raíces
TTBR duales (**F7i**) y switch proceso+TTBR freestanding (**F7j**). `fork`/`exec`
producto, musl aarch64 y link `ALL_OBJS` siguen **BLOCKED** (toolchain / muro
de interrupciones).

CFLAGS de boot incluyen `-mgeneral-regs-only` para no atrapar NEON en EL1 temprano.

## 2. Arquitectura interna

| Capa | Ubicación | Rol |
|------|-----------|-----|
| Fachada portable | `includes/ir0/arch_port.h` | Consultas CPU, habilitación IRQ, fachada port I/O |
| Interfaz común | `arch/common/arch_interface.c` | Despacho cross-arch |
| Primer switch | `first_switch_to` | x86 `user_mode.c`; ARM `first_switch.c` |
| x86-64 | `arch/x86-64/` | boot, IDT, PIC, modo user, syscalls |
| arm64 | `arch/arm64/sources/` | boot_stub, mmu_early, vectors, switch_early, process_early |
| Context switch | `sched/switch/switch_x64.asm`, `switch_arm64.c` | por ISA; ARM llama `arm64_cpu_switch_mm` |
| Config | `setup/Kconfig`, `ARCH=` en Makefile | `ARCH_OBJS_ARM64` incluye `switch_early.S` |

**Tags guard (`architecture_guard.py`):** árboles portables no deben incluir
`<arch/...>` o `<drivers/...>` directamente; usar fachadas.

## 3. Flujo de datos

**Ruta syscall x86-64:**

```text
  musl → insn syscall → syscall_insn_entry_64.asm
       → syscall_dispatch
       → handler → sysret

  debug_bins → int 0x80 → syscall_entry_64.asm → dispatch
```

**Context switch (producto x86):**

```text
  sched_schedule_next → first_switch_to(next)   # primera entrada
                     → switch_to(prev, next)    # posteriores
```

**arm64 freestanding (smoke actual):**

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
- Smokes ARM freestanding no deben reclamar readiness de `process_t` / musl producto.

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
  (completo)  (freestanding + probe)
     │           │
  boot/syscall  boot_stub + switch_early
  switch_x64    process_early / TTBR
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

## 9. Consejos de depuración

- `make build-matrix-min` — compila variantes arch según matrix.
- `make arch-guard` — violaciones de fachada antes de merge.
- `get_arch_name()` / `/proc/cpuinfo` para cadena ISA en runtime.
- Boot arm64: `make smoke-arm64` / `smoke-arm64-syscall` (exige
  `ARM64_PROCESS_TTBR_OK` entre otros tags).
- F7b pack: `make smoke-arm64-port` / `smoke-arm64-gic`.
- Compile portable: `make arm64-portable-compile` (objs curados — **no** `ALL_OBJS`).
- Probe: `make arm64-all-objs-probe` (MEMORY + sample KERNEL compile-only).
- Link scaffold: `make ARCH=arm64 kernel-arm64.bin`.

## 10. Hoja de ruta futura

- `fork`/`exec` ARM producto / `rr_sched` con `process_t` real — **fuera** de la imagen freestanding.
- **ALL_OBJS + musl aarch64** — BLOCKED (toolchain SETUP / muro IRQ).
- GIC detrás de `register_irq` en path producto.
- Eliminar clusters `#ifdef` solo x86 en keyboard/console.
- Boot UEFI en x86 — solo GRUB Multiboot hoy.
- RISC-V / x86-32 — **no en el árbol**.

Legado: `Documentation/DECOUPLING.md`, `arch/README.md`.
