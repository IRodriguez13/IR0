# IR0 Kernel — Testing (estilo kernels de producción)

El directorio `tests/` centraliza todo lo que compila y ejecuta tests. El kernel **solo incluye los tests in-kernel** cuando se construye con `make tests` (se genera `kernel-x64-test.bin` / `kernel-x64-test.iso`). El kernel por defecto (`make ir0`) no lleva el comando `ktest` ni la batería in-kernel.

**Para CI / GitHub:** el workflow `.github/workflows/tests.yml` ejecuta `make tests`, `make kernel-analyze`, `make kernel-memsafe`, `make kernel-tests`, `make build-matrix-min` y `make arch-guard`. Un push o PR a `main`/`mainline`/`master` dispara la batería completa.

## Resumen rápido

| Target              | Qué hace |
|---------------------|----------|
| `make tests`        | Compila kernel-memsafe y **kernel con tests in-kernel** (kernel-x64-test.bin). |
| `make kernel-memsafe` | Compila código del kernel para el host y lo ejecuta bajo **Valgrind** (rutas como resource_registry). |
| `make kernel-tests` | Arranca QEMU con kernel-x64-test.iso; los tests se ejecutan **en PID 1 (init de test)** para cubrir casos dependientes de proceso. Comprueba KTAP y resumen, y falla si hay `SKIP`. |
| `make runtime-mount-check` | Verifica en la salida KTAP de QEMU que los contratos `mount_proc_contract` y `mount_tmpfs_contract` pasen (flujo mount reproducible). |
| `make kernel-analyze` | Analiza el binario del kernel: size, secciones ELF, símbolos; comprueba que kmain exista. Útil para regresión de tamaño y salud. |
| `make health` | Ejecuta toda la batería: kernel-analyze, kernel-memsafe, kernel-tests. Indica buena salud del SO si todo pasa. |
| `make run-gdb`      | Arranca QEMU con servidor **GDB** (puerto 1234) para depurar el kernel (memoria, breakpoints). |

## Estructura de `tests/`

- **tests/kernel_memsafe/** — Código del kernel compilado para el host (con dependencias mínimas/stubs) y ejecutado bajo Valgrind para comprobar estado de memoria en esas rutas.
- **kernel/test/** — Tests que se compilan **dentro** del kernel. Se ejecutan **al arranque** en kernel-x64-test.bin (estilo KUnit) o con el comando `ktest` en la shell. Solo se enlazan al hacer `make tests` / `make kernel-tests`.

## Tests in-kernel al estilo KUnit (producción)

En Linux, **KUnit** ejecuta tests al boot desde un executor que recorre una sección del linker; la salida es **KTAP** (parseable). En IR0:

- **Executor al arranque**: En `kmain()` se llama a `kernel_test_run_all()` (símbolo weak; solo en kernel-x64-test.bin). No hace falta shell ni teclear `ktest`.
- **KTAP por serial**: Se emite `1..N` y por cada test `ok N - name` o `not ok N - name`.
- **make kernel-tests** comprueba que aparezca `All N test(s) passed` y que no haya `not ok`, `Some tests FAILED` ni `# SKIP need process`.

El set base sin proceso (`boot_ok`, `resource_registry`, `allocator`, `path`, `string`) sigue existiendo, pero en kernel de test la ejecución ocurre desde `init_1` para que también corran los dependientes de proceso (syscalls/procfs/process_current/contratos debug bins).

## Cómo se parece a kernels de producción

- **Código kernel en host bajo Valgrind**: Similar a compilar subsistemas del kernel para userspace con stubs y correrlos con Valgrind/ASan (p. ej. algunos tests de lib/ en Linux).
- **Tests in-kernel (ktest)**: Similar a **KUnit** / tests que corren dentro del kernel (en boot o por módulo).
- **GDB + QEMU**: Igual que depurar el kernel con `qemu -s -S` y `gdb target remote :1234`; inspección de memoria, breakpoints, single-step.

## GDB para depurar el kernel

1. En una terminal: `make run-gdb`. QEMU arranca y se queda esperando a GDB.
2. En otra terminal:
   ```bash
   gdb -ex 'target remote :1234' -ex 'symbol-file kernel-x64.bin' kernel-x64.bin
   ```
3. En GDB: `break kmain`, luego `continue`. Puedes inspeccionar memoria (`x/`), variables, backtrace, etc.

Para usar el kernel con tests: construye antes `make tests` y carga símbolos del binario con tests:
   ```bash
   gdb -ex 'target remote :1234' -ex 'symbol-file kernel-x64-test.bin' kernel-x64-test.bin
   ```

## Añadir tests

- **Kernel-memsafe**: Añadir más `.c` del kernel en tests/kernel_memsafe (con stubs si usan kmalloc/kfree u otras dependencias de kernel) y nuevos casos en `test_*.c`.
- **In-kernel (kernel/test/)**: Implementar `void ktest_nombre(void)`, registrarla en `test_runner.c` (y en `ktest_needs_process[]` si requiere proceso), y, si hace falta, un nuevo `kernel/test/test_*.c`.

## Flujo reproducible de mount points (no-root)

Para validar mounts secundarios de forma consistente en shell:

1. `mkdir /mntkt`
2. `mount none /mntkt tmpfs` (o `mount -t tmpfs none /mntkt`)
3. Crear/leer archivos bajo `/mntkt`
4. `mount` para confirmar la línea `/mntkt tmpfs` en `/proc/mounts`

Este flujo se cubre también por contrato en `mount_tmpfs_contract`.

## Flujo multi-FS de convivencia

Para validar coexistencia en puntos de montaje coherentes:

1. `mkdir /mnt && mkdir /mnt/simple && mkdir /mnt/fat`
2. `mount /dev/simple0 /mnt/simple simplefs`
3. `mount /dev/fat0 /mnt/fat fat16`
4. Crear/leer archivos en ambos árboles
5. `mount` y verificar líneas para `simplefs` y `fat16`

Los contratos `mount_multi_fs_contract` y `mount_longest_prefix_contract`
cubren esta ruta en `kernel-tests` y en `runtime-mount-check`.

## ¿Se puede testear todo sin procesos usermode reales?

**Sí, casi todo.** No hace falta init real ni procesos en ring 3 para la batería actual:

- **Sin ningún proceso** (boot desde kmain): Se puede testear todo lo que no use `current_process` — p. ej. resource_registry, allocator, parser de paths, estructuras de datos.
- **Con el init de test** (un proceso, kernel-mode, la shell como PID 1): Si el scheduler llega a ejecutar init_1, ese proceso tiene `current_process` y es KERNEL_MODE. Los syscalls aceptan buffers en memoria kernel (p. ej. el stack del proceso). Por tanto getpid, open, read, pipe, procfs y process_current **sí se pueden testear** desde ese único proceso, sin usermode real. No hace falta `/sbin/init` ni procesos en ring 3.
- **Usermode real** solo es necesario para probar rutas que exigen direcciones de usuario (ring 3) de verdad: p. ej. `copy_from_user`/`copy_to_user` con punteros de usuario, exec desde userspace, fork retornando a usuario. Eso sería otra batería (proceso usuario que invoca syscalls con punteros en su espacio).

Resumen: con **init de test + un proceso** se puede testear todo lo que hoy está en kernel/test. Lo que queda fuera sin usermode real son solo las rutas que validan o copian desde espacio de usuario "de verdad".

El análisis del binario del kernel (símbolos, tamaños, secciones) se hace con **`make kernel-analyze`** (size, readelf, nm) y comprueba que el punto de entrada `kmain` esté presente. Para una comprobación de **buena salud** del SO, usar **`make health`**: ejecuta kernel-analyze, kernel-memsafe y kernel-tests; si todo pasa, el árbol está sano.
