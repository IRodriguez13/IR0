# IR0 con forma Linux (nombres y ABI, no un clon del árbol)

> **Última verificación:** 2026-09-22
> **Fuente de verdad:** `includes/ir0/copy_user.h`, `includes/ir0/arch_cpu.h`,
> `includes/ir0/context.h`, `scripts/architecture_guard.py`,
> [`../DECOUPLING.md`](../DECOUPLING.md), [`../uaccess.md`](../uaccess.md),
> [`../HARNESS_MAP.md`](../HARNESS_MAP.md)

IR0 toma **aprendizajes de Linux** (ABI de syscalls, contratos observables,
nombres de facade) como entrenamiento. **No** clona el layout de directorios
de Linux. El árbol sigue siendo IR0: `sched/` en la raíz, APIs en
`includes/ir0/`, sin dump `include/linux/`.

Canónico en inglés: [`../LINUX_SHAPED.md`](../LINUX_SHAPED.md).

## Qué copiar

| Tomar | Por qué | Dónde en IR0 |
|-------|---------|--------------|
| ABI x86-64 (`rax`/`rdi`…, `-errno`) | musl / BusyBox / man son la spec | `kernel/syscalls/` |
| Capas de usercopy | mm actual vs pgd explícito; nunca CR3+`memcpy` | `copy_user.h` |
| Nombres públicos simples | Código portable no dice `arch_*` en hot paths | `includes/ir0/*.h` |
| Contratos observables | No romper userspace; clasificar antes de parchear ports | `scripts/linux_abi/contracts.json` |

## Qué no copiar

| Evitar | Por qué |
|--------|---------|
| Layout `kernel/sched/` de Linux | `sched/` en raíz es el scheduler de IR0 |
| Dump `include/linux/*.h` | Facades chicas y propias |
| Un `.c` por syscall | Ya hay módulos de dominio |

## Facades de arquitectura

`arch_cpu.h` es **paraguas de compatibilidad**. Código nuevo incluye el
header de dominio (`cpu.h`, `irq.h`, `arch_mm.h`, `context.h`, …).
El paraguas **no** incluye `cpu.h` (conflicto de `cpuid()`).

Entrada a ring 3: `prepare_task_user_iretq` (nombre simple).

## Usercopy

| Capa | Nombre IR0 |
|------|------------|
| mm actual | `copy_to/from_user`, `clear_user` |
| pgd explícito (callers) | `copy_*_user_mm`, `zero_user_mm` |
| primitiva de walk | `*_region_in_directory` |

Detalle: [`../uaccess.md`](../uaccess.md).

## Harness

Tags y Make semánticos: [`HARNESS_MAP.md`](HARNESS_MAP.md).
Se conserva el env ISD `FASE50_BUSYBOX_BIN`, los `fase58_*.config` del
hermano, y `setup/pid1/fase52_staging/`.
