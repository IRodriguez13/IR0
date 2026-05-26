<!-- IR0 AI dev rule: ir0-smoke-autokill -->
<!-- alwaysApply: false -->
<!-- description: All QEMU smokes must exit fast on pass/fail tags via scripts/smoke_qemu_run.sh -->

# IR0 — Smoke Autokill (mandatory)

Every `smoke-*` target that runs QEMU **must** use `scripts/smoke_qemu_run.sh` (`$(SMOKE_QEMU_RUN)` in Makefile).

## Why

Harness inits call `pause()` after success. Raw `timeout N … | tee` waits the full N seconds even when all tags are already in the log.

## Required pattern

```makefile
$(SMOKE_QEMU_RUN) --log $(SMOKE_LOG) --timeout SEC --done TERMINAL_TAG -- \
	$(QEMU) … -serial stdio … ;
```

- **`--done`**: last terminal success tag emitted by the harness (repeat for multiple required tags).
- **`--timeout`**: hard ceiling only (harness hang / no tag); not the expected runtime.
- **Fail-fast**: script kills QEMU on `_FAIL_REASON`, `[FASE*][FAIL]`, `EXEC_ONLY_FAIL`, `BUSYBOX_FAIL` (override with `--fail-regex` if needed).
- **Log to file** (not `tee` to stdout): keeps iteration quiet; inspect `/tmp/userspace-*.log` on failure.

## Anti-patterns

- `timeout 120 $(QEMU) … | tee $(LOG)` for new or touched smokes.
- Piping smoke output through `tail -N` (buffers until QEMU exits).
- Increasing timeout instead of adding `--done` when the harness already prints a terminal tag.

## New harness checklist

1. Emit a unique terminal tag before `pause()` / halt loop.
2. Emit `_FAIL_REASON` or `[FASE*][FAIL]` on failure paths.
3. Wire Makefile smoke with `--done` matching that terminal tag.

## Classification

If QEMU survives past tags but is stable (no leak, no busy loop): `LONG_RUNNING_BUT_STABLE` in harness or Makefile — still **kill QEMU** via autokill; do not rely on full timeout.
