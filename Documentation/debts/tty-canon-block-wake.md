> **Última verificación:** 2026-07-25
> **Fuente de verdad:** `kernel/test/test_debug_contracts.c`, `kernel/main.c`, `includes/ir0/console.c`

# Debt: `tty_canon_block_wake` boot ktest

## Status

**Open — P1.** Removed from the `kmain` boot suite because it hung the guest.

## Symptom

A ktest that puts a reader to sleep on the TTY canon wait queue from early boot
never woke (or woke into a bad state), so QEMU never reached later tests /
userspace.

## Analysis (current)

| Question | Answer |
|----------|--------|
| Invalid because too early? | **Likely yes.** From `kmain`, IRQ1/keyboard delivery and a full schedule loop may not match product paths used after pid1. |
| Lost wakeup? | **Possible but unproven.** Without a post-scheduler repro that fails, do not assume a wait-queue bug. |
| Correct home | **Userspace / QEMU smoke** (preferred) or **KTM after scheduler + input ready**. Not host-only. Not blind re-enable in `kmain`. |

## Reproduction (when reopened)

1. Build with `IR0_KERNEL_TESTS`.
2. Re-add only the wake case behind a late gate (after `schedule` + console ready).
3. Or: userspace: open console raw/canon, block in `read`, inject key via HMP/serial, expect wake + byte.

## Risk if ignored

Canon readers that sleep may hang under rare races; product shell today uses
paths covered by other ktests (`tty_canon_read_immediate`) and interactive smokes.

## Decision for this series

Do **not** re-enable in `kmain`. Track here until a late-gate or smoke exists.
