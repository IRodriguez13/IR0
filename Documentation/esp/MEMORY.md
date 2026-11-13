# Subsistema de Memoria en IR0

> **Última verificación:** 2026-11-07  
> **Fuente de verdad:** [`../MEMORY.md`](../MEMORY.md), [`uaccess.md`](uaccess.md), `mm/swap/`

La memoria en IR0 combina PMM, allocator del kernel y paginacion para
aislamiento de procesos.

## Cambios recientes (2026-09-02)

- Frontera uaccess documentada: [`uaccess.md`](uaccess.md) — toda copia a VA
  user pasa por `copy_*_user` / region helpers (COW-safe); fallos kernel en VA
  user clasifican como `KERNEL_UACCESS_FAULT`.
- Stack: sin soft-grow past `USER_STACK_TOP`; frames de señal vía
  `signal_pick_handler_sp` + margen TOP (detalle en [`../MEMORY.md`](../MEMORY.md)).

## Cambios recientes (2026-09-01)

- Eliminado soft-grow implicito heap/stack en `#PF` (fuera de VMA → SIGSEGV).
- Forensics x86 bajo `DEBUG_D1_DIAG` en `arch/x86-64/sources/pf_debug.c`.
- COW sin cambio de contrato; gate: `make smoke-mm-cow-lazy`.

## Organización del código

`mm/` contiene sólo código portable, agrupado por responsabilidad; las
instrucciones de MMU, TLB y atomics viven en `arch/<isa>/` detrás de
`<ir0/arch_mm.h>`, `<ir0/tlb.h>` y `<ir0/atomic.h>`.

| Directorio | Responsabilidad |
|------------|-----------------|
| `mm/alloc/` | Frames físicos (PMM), asignación acotada, heap del kernel, fachada de estadísticas |
| `mm/boot/` | Inventario de memoria del firmware y entrega al PMM |
| `mm/fault/` | Demand-zero, ruptura de COW, SIGSEGV y fallos del kernel |
| `mm/paging/` | Walker de tablas, fork, identity maps, pool de tablas, política de TLB |
| `mm/uaccess/` | `access_ok`, `copy_{to,from}_user`, copias a usuario seguras con COW |
| `mm/reclaim/` | Registro de reclaimers, política y killer OOM |
| `mm/swap/` | Áreas de swap, E/S y reclaimer de swap |
| `mm/shmem/` | Objetos de `memfd_create` |
| `mm/nommu/` | Política de espacio de direcciones directo sin MMU |
| `mm/` (`diag.c`) | Hooks de `<ir0/mm_diag.h>` |

## Capas Principales

- `mm/alloc/pmm.c`: tracking y asignacion de frames fisicos.
- `mm/alloc/allocator.c` y `includes/ir0/kmem.h`: asignador dinamico del kernel.
- `mm/paging/paging.c`: mapeo virtual y setup de tablas de paginas.

## Modelo Operativo

- El PMM entrega paginas fisicas para kernel y mapeos.
- El allocator del kernel cubre la mayoria de estructuras dinamicas.
- La paginacion da fronteras de address-space y transicion de contexto.

## Integracion con Procesos

- La creacion de procesos enlaza estructuras de memoria por proceso.
- Scheduler/context-switch depende del cambio de estado de paging.
- Validacion de acceso user y helpers de copia refuerzan limites — ver
  contrato canónico [`uaccess.md`](uaccess.md).

## Puntos Fuertes

- Separacion clara de responsabilidades fisica/heap/virtual.
- Estabilidad suficiente para sostener trabajo de estabilizacion en subsistemas.
- Instrumentacion disponible via endpoints proc y logs runtime.

## Puntos Debiles

- Swap anónimo privado existe (`swapon`/`swapoff`, partición o swapfile MINIX);
  no hay zswap, hibernación ni swap file-backed/shared.
- Huge-page COW y políticas POSIX profundas de VM siguen fuera de alcance.
- El tuning para cargas grandes aun no es foco principal.
- Algunas politicas priorizan simplicidad sobre profundidad POSIX completa.
