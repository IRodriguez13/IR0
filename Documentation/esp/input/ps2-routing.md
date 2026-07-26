> **Última verificación:** 2026-07-25
> **Fuente de verdad:** mismos paths que la versión en inglés
> **Canónico:** `Documentation/input/ps2-routing.md`

# Enrutado PS/2 teclado/mouse (i8042)

## Leyenda de estado

| Etiqueta | Significado |
|----------|-------------|
| **Implemented** | Presente en el árbol y con evidencia host y/o QEMU |
| **Partial** | Funciona en el path de producto; hay huecos documentados |
| **Planned** | No implementado; solo deuda |

## Resumen

IR0 usa scan code set **1** (teclado) y paquetes PS/2 de mouse de 3/4 bytes.
Teclado y mouse comparten el puerto de datos i8042 (`0x60`); la demultiplexación
usa el bit **AUXDATA (0x20)** del status (`0x64`).

## Demux i8042 — Implemented

```text
IRQ1 / IRQ12 / idle poll
  → leer status (0x64)
  → si OUTPUT_FULL: leer data (0x60)
  → si AUXDATA: input_mouse_feed_byte() → ensamblador de paquete
  → si no: keyboard_feed_scancode() → TTY / EV_KEY
```

- La clasificación ocurre **antes** de cualquier decoder de teclado.
- IRQ1 e IRQ12 llaman al mismo `keyboard_poll_ps2()`.
- El init del mouse habilita `PS2_CFG_INT2`.

## Modificadores — Implemented

Estado L/R independiente en `struct keyboard_modifiers`.
`ctrl = left_ctrl || right_ctrl`. Los break codes limpian el lado correspondiente.

## Paquetes de mouse — Implemented

Resync con bit 3 del header; solo se publican paquetes completos.
El path de mouse no muta modificadores del teclado.

## `/dev/events0` — Partial

Un solo anillo sin `device_id`. Deuda: `Documentation/debts/events0-device-id.md`.

## Invariantes

1. El mouse no emite bytes de TTY.
2. Los bytes AUX no alimentan `ps2_set1_feed`.
3. Los modificadores se liberan con sus break codes reales.
