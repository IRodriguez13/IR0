# Contratos de guest que fija ktest

> **Última verificación:** 2026-11-03
> **Fuente de verdad:** `kernel/test/test_fix_contracts.c`,
> `kernel/test/test_mm_contracts.c`, `kernel/test/test_runner.c`,
> `includes/ir0/futex.h`, `includes/ir0/ipc.h`, `includes/ir0/arch_mm.h`,
> `kernel/sync/futex.c`, `ipc/ipc.c`, `mm/page_fault.c`, `mm/paging.c`,
> `arch/x86-64/sources/arch_mm.c`, `arch/arm64/sources/arch_mm.c`

Canónico en inglés: [`../../testing/KTEST_FIX_CONTRACTS.md`](../../testing/KTEST_FIX_CONTRACTS.md).

`make kernel-tests` arranca `kernel-x64-test.bin` y reporta por el escenario KTM
`kernel.ktest`. El 2026-11-03 la suite imprimió `All 80 test(s) passed` y
`TEST_END|kernel.ktest|PASS`. El pánico posterior `Failed to load /sbin/init`
es el ISO de test, después de ese veredicto.

Quince de esos casos viven en `kernel/test/test_fix_contracts.c`. Fallan el
boot si el comportamiento de abajo regresa. Los casos de COW de abajo viven en
`kernel/test/test_mm_contracts.c`. El resto de la suite es anterior y no se
lista aquí.

## Cómo correrlo

```bash
make kernel-tests
```

El runner es `scripts/make/ktm.mk` (`SMOKE_QEMU_RUN`, 128 MiB de guest,
timeout de 60 s). El log se copia bajo `$TMPDIR` o `/tmp` y puede contener
bytes NUL; buscarlo con `rg -a`.

## Casos

| Nombre KTAP | Qué significa un pass |
|-------------|------------------------|
| `readlink_returns_copied_bytes` | `sys_readlink` de `/proc/self/fd/0` (o `/proc/self/exe` si ese enlace mide menos de 2 bytes) devuelve la longitud copiada. Un buffer de 1 byte devuelve `1`. |
| `fstat_redirected_stdio_is_file` | Tras `dup2` de un archivo regular de tmpfs sobre el fd 0, `sys_fstat` llena `struct ir0_user_stat` (`includes/ir0/stat_user.h`) y el modo es archivo regular, no dispositivo de caracteres. |
| `lseek_redirected_stdio_uses_file` | Tras `dup2` de un archivo de 4 bytes sobre el fd 1, `sys_lseek(1, 0, SEEK_END)` devuelve `4`. `dup2` devuelve el fd nuevo. |
| `read_efault_keeps_offset` | Un `sys_read` en `USER_MODE` hacia un rango válido pero sin mapear devuelve `-EFAULT` sin avanzar el offset del archivo. La lectura siguiente devuelve los dos bytes escritos (`AB`). |
| `fsync_null_fd_table` | Envenenar `files_struct.magic` hace que `sys_fstat`, `sys_fsync` y `sys_ftruncate` devuelvan `-EBADF`. El magic se restaura. |
| `rename_keeps_unlink_errno` | `sys_rename` de un archivo sobre otro en un tmpfs de solo lectura devuelve `-EROFS`. |
| `pipe_read_efault_keeps_bytes` | Con el extremo de escritura cerrado, un `sys_read` en `USER_MODE` hacia una dirección de usuario no mapeada devuelve `-EFAULT`. La lectura siguiente devuelve los dos bytes escritos (`AB`). |
| `socket_read_efault_keeps_bytes` | Un `sys_read` en `USER_MODE` sobre un stream `AF_UNIX` hacia una dirección no mapeada devuelve `-EFAULT`. La lectura siguiente devuelve los dos bytes enviados, así que la copia fallida no los sacó de la cola. |
| `rt_sigsuspend_consumes_pending` | Con `SIGUSR1` pendiente y un handler que no es `SIG_DFL`, ni `SIG_IGN`, ni una dirección de usuario, `sys_rt_sigsuspend` devuelve `-EINTR`, limpia ese bit pendiente, restaura la máscara anterior y deja `signal_mask_saved_valid` en cero. |
| `futex_interrupted_wait_frees_slot` | Un wait marcado como interrumpido antes de dormir devuelve `-EINTR`. Un `ir0_futex_wake` posterior de esa palabra devuelve `0` (el slot está libre). |
| `futex_exit_drop_frees_slot` | `ir0_futex_drop_process` durante el wait libera el slot. El wake hecho desde el gancho devuelve `0`, y un wake después del wait también devuelve `0`. |
| `ipc_read_rechecks_after_enqueue` | Un canal con un escritor y el ring vacío entrega un byte escrito después de encolar al waiter. La tarea queda en `PROCESS_READY`. |
| `unmap_splits_2mb_leaf` | En un directorio de páginas privado (no el CR3 vivo), desmapear la base de una hoja de identidad de 2 MiB limpia esa página de 4 KiB y deja mapeada la página de 4 KiB siguiente. |
| `map_replaced_frame_is_released` | Reemplazar un PTE con otro frame baja el refcount del frame viejo a 0. Reescribir el mismo frame deja el refcount en 1. El unmap lo baja a 0. |
| `map_fixed_aborts_when_unmap_fails` | `MAP_FIXED` sobre dos páginas anónimas devuelve `-ENOMEM` cuando el backend de unmap rechaza la segunda. La primera deja de estar mapeada. El valor del PTE de la segunda no cambia. |

Los casos de COW son nuevos y viven en `kernel/test/test_mm_contracts.c`:

| Nombre KTAP | Qué significa un pass |
|-------------|------------------------|
| `cow_write_fault_drops_old_once` | Un fault de escritura sobre un PTE `PAGE_COW` reemplaza el frame y libera el frame viejo exactamente una vez (sin el pánico `PMM: double free detected`). El PTE nuevo es escribible y no tiene `PAGE_COW`, y el contenido de la página sobrevive la copia. |
| `copy_to_user_cow_break_drops_old_once` | Un `copy_to_user_mm` sobre una página `PAGE_COW` rompe COW por el camino de uaccess y libera el frame viejo exactamente una vez (sin double free). El PTE nuevo es escribible y no tiene `PAGE_COW`, y los bytes escritos se leen de vuelta con `copy_from_user_mm`. |

Un double free paniquea el guest antes de que la suite imprima su veredicto,
así que una regresión falla todo el run de `make kernel-tests`.

## Ganchos de test

La tarea de boot es la única ejecutable. Un sleep real cuelga la suite, y
plantar una hoja enorme en el CR3 vivo no es un fixture seguro. Tres
punteros a función quedan en `NULL` salvo que un test los instale. Los
caminos de producción no pasan por un gancho distinto de `NULL`.

| Símbolo | Dónde corre | Contrato |
|---------|-------------|----------|
| `ir0_futex_before_sleep` | Al empezar un `ir0_futex_wait` bloqueado, antes de `sched_schedule_next` | Puede marcar la tarea `PROCESS_READY`. No debe llamar a `ir0_futex_wait`. |
| `ipc_sleep_recheck_probe` | Después de `wait_queue_add`, antes de volver a mirar el ring | No debe dormir ni planificar. |
| `mm_backend_unmap_hook` | Lo primero en `mm_backend_unmap_user_page` en x86-64 y ARM64 | Devolver `-EOPNOTSUPP` para caer al backend de la ISA. Cualquier otro valor es el resultado del unmap y el walker genérico no corre. |

`unmap_page_in_directory` ya devuelve un resultado de backend distinto de
`-EOPNOTSUPP` sin caminar las tablas. El caso `MAP_FIXED` usa eso: el gancho
devuelve `-EIO` para una dirección y `-EOPNOTSUPP` para el resto.
`is_page_mapped_in_directory` no consulta el gancho, así que la página sigue
pareciendo mapeada y `mm_prepare_map_fixed` registra el fallo.

Con el gancho en `NULL`, `mm_backend_unmap_user_page` de x86-64 sigue
devolviendo `-EOPNOTSUPP`. ARM64 sigue desmapeando solo un root que le
pertenece y, si no, devuelve `-EOPNOTSUPP`.

## Qué no afirma esta suite

- `rt_sigsuspend` no construye un frame de señal de usuario. La tarea de
  ktest no tiene puntero de instrucción de usuario. El caso prueba que la
  entrega bajo la máscara temporal limpia el bit pendiente y restaura la
  máscara anterior cuando `process_signal_enter_pending` queda en cero.
- El caso de 2 MiB usa un directorio privado. No desmapea la hoja de
  identidad de 1 GiB del CR3 vivo. `map_page_in_directory` sigue sin poder
  partir esa hoja.
- Pasar `make kernel-tests` es la prueba de guest en x86-64. No es un boot ARM64.
