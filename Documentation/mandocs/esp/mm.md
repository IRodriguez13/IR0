# Gestión de memoria de IR0

| Campo | Valor |
|-------|-------|
| Versión | 0.3 |
| Fase IR0 | T0–T1 |
| Estado | stable |
| Depende de | boot, process, syscalls |
| Página man | ir0-mm (sección 7) |
| Fuentes principales | `mm/pmm.c`, `mm/paging.c`, `mm/allocator.c`, `mm/kmem.c`, `kernel/syscalls/mm_syscalls.c`, `kernel/elf_loader.c`, `arch/x86-64/sources/fault.c` |

> **Última verificación:** 2026-07-10  
> **Fuente de verdad:** `copy_process_memory`, `pmm_frame_get`/`put`, `fault.c`, `make smoke-mm-cow-lazy`

## 1. Visión general

IR0 usa un layout fijo de memoria baja: región de arranque mapeada por identidad,
heap dedicado del kernel, pool de frames PMM y directorios de páginas por proceso
para tareas de usuario. **No hay higher-half del kernel**; kernel y usuario usan
ambos VAs canónicas bajas con PML4s separados para procesos de usuario.

## 2. Arquitectura interna

| Capa | Archivo | Rol |
|------|---------|-----|
| PMM | `mm/pmm.c` | Bitmap 4 KiB + refcount (`pmm_alloc_frame`, `pmm_frame_get`/`put`) |
| Heap | `mm/allocator.c` | Allocador boundary-tag en `0x800000`, 24 MiB |
| kmem | `mm/kmem.c` | `kmalloc`/`kfree` → `alloc()` |
| Paging | `mm/paging.c` | `map_page_in_directory`, share-on-fork (`PAGE_COW`), auditoría OOM |
| Fault | `arch/x86-64/sources/fault.c` | Lazy heap/stack; break-COW en write fault |
| Syscalls | `kernel/syscalls/mm_syscalls.c` | `sys_mmap`, `sys_brk`, `sys_munmap` |
| ELF | `kernel/elf_loader.c` | PT_LOAD vía `map_user_region_in_directory` |

**Layout fijo (`config.h` / boot):**

```text
  0 ─────────────── 48 MiB   mapa identidad (boot)
  0x800000 ─────── 0x2000000  heap kernel (24 MiB)
  0x2000000 ────── 0x3000000  pool PMM (16 MiB)
  USER_HEAP_BASE   0x2000000  inicio brk
  USER_MMAP_START  0x8000000  arena mmap
  tope pila user   ~0x7FFFF000
```

## 3. Flujo de datos

**Asignación kernel:** `kmalloc` → `alloc()` en `[0x800000, 0x2000000)`.

**Puesta en marcha de proceso user (ELF):**

1. `spawn_user` → nuevo `page_directory` (PML4 aislado).
2. Fase 1: mapear todo PT_LOAD con `map_user_region_in_directory` bajo **CR3 kernel**.
3. Fase 2: copiar bytes de segmento / zero BSS vía acceso a frame físico.
4. `elf_setup_stack` — argc/argv/envp en pila de usuario.
5. `sched_add_process`.

**`sys_brk`:** extiende desde `USER_HEAP_BASE` hasta `USER_HEAP_MAX_SIZE` (tope 256 MiB).

**`sys_mmap`:**

```text
  sys_mmap
     ├─ MAP_ANONYMOUS → map pages + mmap_list VMA
     ├─ respaldado por fichero → ruta read VFS + map
     └─ /dev/fb0 (devfs id 15, CONFIG_ENABLE_VBE)
            → ir0_fb_get_info()
            → map physical FB pages PAGE_USER|PAGE_RW
            → entrada mmap_list
```

**Page fault (`arch/x86-64/sources/fault.c`):** valida contra rangos de heap user y VMA mmap;
también rompe COW en write fault (`PAGE_COW` + !`PAGE_RW`).

**Fork (`copy_process_memory`):** share-on-fork de hojas user presentes; páginas RW → RO +
`PAGE_COW` (bit 9) en padre e hijo; `pmm_frame_get`. Prueba: `make smoke-mm-cow-lazy`.
Fuera de alcance: COW huge-page / file-backed.

## 4. Responsabilidades

- PMM: rastrear frames físicos solo en el pool configurado.
- Paging: nunca asumir `active_cr3` cuando el objetivo es otro proceso — usar `page_directory` explícito.
- `copy_user`: recorrer tablas de páginas del **proceso actual** vía fachadas MM.
- ELF loader: completar mapeo antes de añadir al scheduler.

## 5. Límites del subsistema

- `fs/` no debe `#include <mm/...>` — usar `ir0/mm_port.h`, `ir0/kmem.h`.
- `mm/` no debe `#include <arch/...>` — usar `ir0/arch_port.h`.
- Copias user desde rutas pipe/wake deben usar `copy_*_region_in_directory(pml4, …)`.

## 6. Relaciones con otros subsistemas

| Vecino | Interacción |
|--------|-------------|
| Boot | Mapa identidad en `boot_x64.asm`; rango init PMM desde `kmain` |
| Process | `page_directory`, `mmap_list`, `owns_page_directory` |
| VFS | ELF `vfs_read_file`; I/O mmap respaldado por fichero |
| Devfs | Mmap framebuffer vía id dispositivo fb0 |
| Syscalls | brk/mmap/mprotect/munmap |

## 7. Mapas visuales

```text
  RAM física
  ┌────────────────────────────────────┐
  │ 0–48M identidad (acceso boot/kernel)│
  │ heap 8–32M   frames PMM 32–48M     │
  └────────────────────────────────────┘
           ▲                    ▲
           │ kmalloc            │ pmm_alloc_frame
           │                    │
  PML4 por proceso ──map_user_region──► user PT_LOAD / mmap / stack
```

## 8. Invariantes importantes

1. PMM gestiona solo `PMM_PHYS_BASE`..`PMM_PHYS_SIZE` (predeterminado 32–48 MiB).
2. El heap del kernel está acotado; no crece hacia PMM en runtime.
3. Aislamiento user/kernel vía PML4 separado por proceso (excepto idle comparte CR3 kernel).
4. Rutas OOM clasificadas: `FASE43` boot_fatal / kernel_fatal / user_recoverable.
5. `process_destroy` desmapea bajo **PML4 del proceso**, no necesariamente CR3 activo.

## 9. Consejos de depuración

Tags: `[PMM]`, `[FASE43][OOM_CLASS]`, `SERIAL: ELF: Mapping segment`, `FB_MMAP_*`.

- `/proc/meminfo` — estadísticas de memoria en runtime (texto generado procfs).
- Bucles de page fault: comprobar mmap no mapeado o desbordamiento de heap.
- Fallo mmap framebuffer: verificar `CONFIG_ENABLE_VBE` y apertura fb0.

## 10. Hoja de ruta futura

- COW de huge-page (2 MiB) y file-backed — no implementado.
- Demand paging para mmap anónimo — parcial (`CONFIG_LAZY_*`).
- VA higher-half del kernel — no planificada en el árbol actual.
- Pool PMM mayor / descubrimiento dinámico de memoria física más allá de deuda del mapa mem Multiboot.
