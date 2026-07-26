> **Última verificación:** 2026-07-25
> **Fuente de verdad:** mismos paths que la versión en inglés
> **Canónico:** `Documentation/tty/terminal-contract.md`

# Contrato de terminal / TTY

## Geometría — Implemented

`TIOCGWINSZ` / `TIOCSWINSZ` vía `ir0_console_ioctl_winsize*`.
`console_get_geometry()` siempre está enlazado (VGA y framebuffer); no debe
vivir solo bajo `#if CONFIG_ENABLE_VBE`.

## Termios — Partial

ICANON, ECHO, ISIG, VMIN/VTIME y caracteres de control básicos.
No es un driver TTY Linux completo.

## Entrada de control — Implemented

| Secuencia | Resultado |
|-----------|-----------|
| Ctrl-X | `0x18` en raw; sin Ctrl pegado tras break |
| Ctrl-C | SIGINT con ISIG |
| Flechas | CSI vía set-1 E0 |

Probe: `setup/pid1/tty_raw_probe.c` + `scripts/smoke_tty_raw_probe.py`.

## TERM — Partial

Producto / nano smoke: **`TERM=linux`**.

## Salida de aplicación — Partial

La app restaura termios con `tcsetattr`. Tras nano, verificar shell con
`echo TERMINAL_OK` / `stty -a`.

## Panic en pantalla — Implemented

`panicex()` limpia la consola activa (FB o VGA) e imprime banner + causa en
pantalla y serial.

## Deuda: `tty_canon_block_wake`

Ver `Documentation/debts/tty-canon-block-wake.md`.
