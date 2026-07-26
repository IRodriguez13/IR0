> **Última verificación:** 2026-07-25
> **Fuente de verdad:** `drivers/video/typewriter.c`, `scripts/smoke_desktop_nano_mnt.py`

# Debt: #UD after GNU nano save/exit (fitted FB geometry)

## Status

**Mitigated — P1 watch.** `smoke-desktop-nano-mnt` PASS after scrollback widen (2026-07-25).

## Symptom (historical)

After nano Ctrl-X → save → `[ Wrote 1 line ]`, guest could panic with `#UD` and a heap RIP (`cs=8`, RIP outside kernel text).

## Cause

Fitted Terminus geometry (~87×27) while typewriter scrollback was still `VGA_WIDTH` (80) → OOB reads on redraw under CSI-heavy nano exit.

## Fix applied

`typewriter.c`: scrollback `[SCROLLBACK_LINES][CONSOLE_MAX_WIDTH]` + column clamps via `scrollback_cols()`.

## Residual risk

If #UD returns with matching heap RIP at 80-wide scrollback, treat as Class B (`want_kernel_ret` / wait–exit), not a nano CSI hack.
