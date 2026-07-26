> **Última verificación:** 2026-07-25
> **Fuente de verdad:** mismos paths que la versión en inglés
> **Canónico:** `Documentation/filesystems/virtual-file-semantics.md`

# Semántica de lectura de archivos virtuales

## Clases

| Clase | open | read | EOF | reopen |
|-------|------|------|-----|--------|
| **Snapshot** | Captura buffer (por open) | Offset + parcial | `0` si offset ≥ len | Nueva captura |
| **Bounce snapshot** | Sin buffer; regenera y corta por offset | Mismo EOF | Mismo | Carrera si el backend cambia |
| **Ring no consumptivo** | N/A | Copia sin dequeue | `0` al fin de la vista | Puede ver líneas nuevas |
| **Stream / events** | N/A | Block o `EAGAIN` | Casi nunca | Continúa |

## Snapshot por open — Implemented (`/dev/net`, `/dev/kmsg`)

```text
open  → capturar snap → vfs_file = snap (refs=1)
read  → leer desde snap + avanzar offset del fd
close → release
dup   → acquire; offset compartido
fork  → acquire; el hijo hereda el offset
```

- Sin offset global entre procesos.
- Dos `open()` → buffers y offsets independientes.

Tests: `ktest_dev_net_contract`.

## `/proc` vía pseudo_fs — Implemented (bounce)

`pseudo_fs_ops_read` regenera, corta por offset y hace EOF.

## Tabla de nodos (actual)

| Path | Clase | Notas |
|------|-------|-------|
| `/dev/net` | Snapshot por open | Texto; reopen para vista fresca |
| `/dev/kmsg` | Snapshot por open | Captura `klog_read_records` al open |
| `/proc/kmsg` | Bounce + ring no consumptivo | **No** hace dequeue; distinto de `/dev/kmsg` |
| `/proc/cpuinfo` | Bounce snapshot | |
| `/proc/version` | Bounce snapshot | |
| `/proc/drivers` | Bounce snapshot | |
| `/proc/<pid>/status` | Bounce snapshot | |
| `/dev/events0` | Stream / events | No es snapshot de texto |
| `/dev/console` stdin | Stream (TTY) | |

## `/dev/kmsg` vs `/proc/kmsg`

Son nodos distintos. Ambos no consumen el ring; `/dev/kmsg` usa snap por open;
`/proc/kmsg` usa bounce + offset.

## dup / fork — Implemented

| Op | Comportamiento |
|----|----------------|
| `dup`/`dup2` | Misma open file description; offset compartido; acquire |
| `fork` | Hereda; acquire; offset compartido hasta otro open |
| Segundo `open` | Snap y offset independientes |
