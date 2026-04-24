# Subsistema de Memoria en IR0

La memoria en IR0 combina PMM, allocator del kernel y paginacion para
aislamiento de procesos.

## Capas Principales

- `mm/pmm.c`: tracking y asignacion de frames fisicos.
- `mm/allocator.c` y `includes/ir0/kmem.h`: asignador dinamico del kernel.
- `mm/paging.c`: mapeo virtual y setup de tablas de paginas.

## Modelo Operativo

- El PMM entrega paginas fisicas para kernel y mapeos.
- El allocator del kernel cubre la mayoria de estructuras dinamicas.
- La paginacion da fronteras de address-space y transicion de contexto.

## Integracion con Procesos

- La creacion de procesos enlaza estructuras de memoria por proceso.
- Scheduler/context-switch depende del cambio de estado de paging.
- Validacion de acceso user y helpers de copia refuerzan limites.

## Puntos Fuertes

- Separacion clara de responsabilidades fisica/heap/virtual.
- Estabilidad suficiente para sostener trabajo de estabilizacion en subsistemas.
- Instrumentacion disponible via endpoints proc y logs runtime.

## Puntos Debiles

- Features VM avanzadas siguen limitadas (por ejemplo, COW/swap de nivel full).
- El tuning para cargas grandes aun no es foco principal.
- Algunas politicas priorizan simplicidad sobre profundidad POSIX completa.
