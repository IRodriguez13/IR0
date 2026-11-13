<!-- IR0 AI dev rule: ir0-source-layout -->
<!-- alwaysApply: true -->
<!-- description: Cada subsistema guarda sólo código genérico, en subdirectorios por responsabilidad; lo de ISA vive en arch/ detrás de fachadas con nombres de Linux -->

# IR0 — Layout del árbol: genérico vs arquitectura

Criterio del mantenedor para todo el kernel. Complementa
`ir0-api-public-private.md` (capas y nombres de fachadas),
`ir0-scale-within-patterns.md` (los patrones se hacen cumplir con guards) e
`ir0-arm64-rpi5-x86-regression-gate.md`.

## Sólo código genérico en los subsistemas

- `kernel/`, `mm/`, `fs/`, `net/`, `sched/`, `ipc/`, `drivers/`, `ktm/`,
  `sound/` e `includes/ir0/` contienen **sólo código genérico**.
- Lo específico de una ISA vive en `arch/<isa>/` (`.c`, `.S`, `.asm`, headers
  en `arch/<isa>/include/asm/`): asm inline, atomics o spins hechos a mano,
  barreras de CPU, registros, formatos de tablas de páginas, instrucciones de
  TLB, acceso a registros de la MMU.
- El código genérico llama una fachada con nombre simple de Linux
  (`<ir0/atomic.h>`, `cmpxchg`, `ir0_spinlock_t`, `smp_mb`, `switch_to`,
  `flush_icache_range`). La fachada incluye `<asm/...>` y cada ISA elige su
  implementación.
- Cuando todas las ISAs se implementarían igual (p. ej. atomics sobre los
  builtins del compilador), un respaldo genérico en `includes/asm-generic/`
  es válido, como `asm-generic` en Linux; cada `arch/<isa>/include/asm/`
  lo selecciona y documenta por qué alcanza.

```text
❌ BAD:  mm/reclaim.c: if (__atomic_test_and_set(&active, __ATOMIC_ACQUIRE))
❌ BAD:  drivers/x.c:  __asm__ volatile("" ::: "memory");
✅ GOOD: mm/reclaim/reclaim.c: if (test_and_set_bit_lock(0, &reclaim_active))
✅ GOOD: drivers/x.c:  barrier();
```

## Subdirectorios por responsabilidad

- Cada subsistema se organiza en subdirectorios atómicos de archivos
  relacionados; cada archivo tiene relación con su subdirectorio. Ejemplo
  (`mm/`):

  | Subdirectorio | Responsabilidad |
  |---------------|-----------------|
  | `mm/alloc/` | marcos físicos (PMM), política de asignación, heap del kernel |
  | `mm/boot/` | inventario de memoria del firmware y entrega al PMM |
  | `mm/fault/` | política de page faults (demand-zero, COW, SIGSEGV) |
  | `mm/paging/` | walker de tablas, identity maps, pool de tablas, TLB |
  | `mm/uaccess/` | `access_ok` / `copy_*_user` |
  | `mm/reclaim/` | registro de reclaimers y OOM killer |
  | `mm/swap/`, `mm/shmem/`, `mm/nommu/` | swap, memfd, política sin MMU |

- El nivel superior del subsistema queda para lo transversal (p. ej.
  `mm/diag.c`, los hooks de `<ir0/mm_diag.h>`).
- Los headers privados (`*_internal.h`) viajan con sus archivos. Entre
  subdirectorios se incluyen con la ruta completa (`<mm/paging/paging_internal.h>`).
- Mover archivos es un commit propio de **pure move**: mismo orden de enlace,
  objetos stripped idénticos en x86_64 y arm64 (salvo `__FILE__`,
  `__LINE__` y `IR0_BUILD_DATE`, verificados por desensamblado). Después
  `python3 scripts/include_prune.py` sobre los archivos movidos.
- Al mover, se actualizan en el mismo commit: listas de objetos del
  `Makefile`, `scripts/make/*.mk`, `scripts/kconfig/subsystems.json`,
  `tests/host/Makefile`, globs y ledgers de `scripts/architecture_guard.py`;
  la documentación que cita las rutas, en un commit de docs aparte.

## Enforcement

- `scripts/architecture_guard.py` `[no-raw-atomics]`: rechaza `__atomic_*`,
  `__sync_*` y `asm`/`__asm__` fuera de `arch/` y de
  `includes/asm-generic/atomic.h`, con auto-test de violación inyectada. Su
  ledger de excepciones (`RAW_ATOMIC_LEDGER`) está vacío y sólo se achica.
- `[portable-no-isa-asm]`, `[portable-no-register-names]` y
  `[paging-neutral-root]` cubren mnemónicos, registros y vocabulario x86 en
  código común.
- Un hallazgo de layout que no tiene guard es deuda: se anota y se automatiza.
