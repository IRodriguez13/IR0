# IR0 Debug Shell (debug_bins) — eliminado

| Campo | Valor |
|-------|-------|
| Version | 0.3 |
| Fase IR0 | histórico |
| Estado | **eliminado del árbol** (2026-07-25) |
| Depende de | — |
| Man page | IR0-debug-bins (sección 7) |
| Fuentes principales | solo historial git; producto: [`USERSPACE.md`](../../USERSPACE.md) / [`esp/USERSPACE.md`](../../esp/USERSPACE.md) |

> **Última verificación:** 2026-07-25

## 1. Resumen

El **dbgshell** in-kernel (`debug_bins/`, `kernel/init.c` / `start_init_process`)
era el REPL de laboratorio Tier-0. **Ya no está en el árbol IR0**.

Producto y exploración usan el hermano **[IR0-userspace](https://github.com/IRodriguez13/IR0-userspace)**
(runit → getty/ash) y contratos del kernel (`make kernel-tests`, auditorías linux-abi).

`make run-dbgshell` termina con mensaje de retiro. No existen símbolos
`CONFIG_KERNEL_DEBUG_SHELL` / `CONFIG_DEBUG_BINS*` en Kconfig.

## 2. Mapa de reemplazo

| Rol anterior | Camino actual |
|--------------|---------------|
| REPL PID1 | `/sbin/init` (runit) vía `kexecve` |
| Comandos shell | applets BusyBox ash en IR0-userspace |
| Contratos syscall/proc | `kernel/test/*`, `make kernel-tests` |
| Exploración boot/kmsg | ash + `cat /proc/kmsg` ([`KLOG.md`](../../KLOG.md)) |
| Docs de acoplamiento | [`USERSPACE.md`](../../USERSPACE.md), `userspace/README.md` |

## 3. No hacer

- Reintroducir `debug_bins/` o un shell mono in-kernel sin oleada dedicada + OK del mantenedor.
- Tratar porcentajes viejos de “debug_bins” como readiness de producto.
- Documentar `CONFIG_KERNEL_DEBUG_SHELL=y` como perfil de producto soportado.

## 4–10. Nota histórica

Versiones anteriores de este capítulo describían grupos Kconfig, `dbgshell.c` y
`debug_bins_registry.c`. Esas fuentes solo existen en el historial git anterior
a la eliminación en `chore/kernel-userspace-boundary`. Este capítulo ya no
describe código vivo.
