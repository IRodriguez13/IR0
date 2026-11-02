# Gestión de memoria de IR0

| Campo | Valor |
|-------|-------|
| Versión | 0.1 |
| Fase IR0 | T0–T1 |
| Estado | stable |
| Depende de | boot, process, syscalls |
| Página man | IR0-memory (sección 7) |
| Fuentes principales | `mm/pmm.c`, `mm/paging.c`, `mm/allocator.c`, `mm/kmem.c`, `mm/oom.c`, `mm/oom_kill.c`, `mm/page_fault.c`, `kernel/syscalls.c`, `kernel/elf_loader.c` |

## 1. Visión general

IR0 usa un layout fijo de memoria baja: región de arranque mapeada por identidad,
heap dedicado del kernel, pool de frames PMM y directorios de páginas por proceso
para tareas de usuario. **No hay higher-half del kernel**; kernel y usuario usan
ambos VAs canónicas bajas con PML4s separados para procesos de usuario.

## 2. Arquitectura interna

| Capa | Archivo | Rol |
|------|---------|-----|
| PMM | `mm/pmm.c` | Bitmap de frames 4 KiB (`pmm_alloc_frame`, `pmm_free_frame`) |
| Heap | `mm/allocator.c` | Allocador boundary-tag en `0x800000`, 24 MiB |
| kmem | `mm/kmem.c` | `kmalloc`/`kfree` → `alloc()` |
| Paging | `mm/paging.c` | `map_page_in_directory`, mapas user/supervisor, auditoría OOM |
| Política OOM | `mm/oom.c` | Badness/eligibilidad pura y orden determinista de víctimas |
| Killer OOM | `mm/oom_kill.c` | Recorrido del registro, `SIGKILL`, hook de reintento (`oom_handle_alloc_failure`) |
| Syscalls | `kernel/syscalls.c` | `sys_mmap`, `sys_brk`, `sys_munmap` |
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

**Page fault (`arch/x86-64/sources/fault.c`):** valida contra rangos de heap user y VMA mmap.

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
4. Política OOM: las candidatas deben ser tareas user, vivas, con `mm`,
   `pid > 1` y `oom_score_adj > -1000`. La badness es páginas residentes +
   páginas de tablas, escalada por `oom_score_adj`; la tarea que asigna se
   marca como origen y gana la selección.
5. El fallo de asignación user (`mm/page_fault.c`) elige una víctima y
   reintenta una vez, como Linux `__alloc_pages_slowpath` tras fallar reclaim.
6. `process_destroy` desmapea bajo **PML4 del proceso**, no necesariamente CR3 activo.

## 9. Consejos de depuración

Tags: `[PMM]`, `[PF] COW OOM`, `[OOM] kill`, `SERIAL: ELF: Mapping segment`, `FB_MMAP_*`.

- `/proc/meminfo` — estadísticas de memoria en runtime (texto generado procfs).
- `/proc/<pid>/oom_score_adj` — sesgo escribible (`-1000..1000`; `-1000` excluye).
- `/proc/<pid>/oom_score` — badness normalizada (`0..1000`) desde un snapshot fijado.
- Bucles de page fault: comprobar mmap no mapeado o desbordamiento de heap.
- Fallo mmap framebuffer: verificar `CONFIG_ENABLE_VBE` y apertura fb0.

## 10. Hoja de ruta futura

- Fork copy-on-write — **no implementado** (copia completa hoy).
- Demand paging para mmap anónimo — parcial; el manejador de fault cubre algunos casos.
- Killer OOM — implementado para fallos user de demand-zero/COW/identity-promote;
  las asignaciones kernel siguen devolviendo `NULL` para que el llamador decida.
- VA higher-half del kernel — no planificada en el árbol actual.
- Pool PMM mayor / descubrimiento dinámico de memoria física más allá de deuda del mapa mem Multiboot.
