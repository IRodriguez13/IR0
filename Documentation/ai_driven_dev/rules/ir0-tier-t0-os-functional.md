<!-- IR0 AI dev rule: ir0-tier-t0-os-functional -->
<!-- alwaysApply: false -->
<!-- description: T0 (~80%) — OS funcional, debug_bins, pseudo-FS registry, syscall contracts -->

# T0 — OS Funcional + debug_bins

## Goal

Close the last ~20%: registry migration, devfs lifecycle, contract tests, proc/sys parity with Linux expectations for diagnostics.

## Research triggers (web)

- `/proc` file formats: Linux `proc(5)`, `Documentation/filesystems/proc.txt` (kernel.org).
- `devfs` / char device semantics: Linux `device.txt`, evdev when touching `/dev/events0`.

## Multi-agent split

1. **Agent A**: Migrate next proc/sys batch to `pseudo_fs_register` (cpuinfo, blockdevices, netinfo, `/proc/pid/status`).
2. **Agent B**: Extend ktests/host tests for each migrated path.
3. **Agent C**: `architecture_guard.py` + DECOUPLING.md table update.

## Done criteria

- Path served by registry OR legacy fallback documented.
- `debug_bins` use **syscalls only** (see `AGENTS.md`).
- No new `strcmp` ladders without a registry migration ticket.

## Priority backlog

1. proc/sys routes still on `switch(fd)`.
2. `devfs_fd_can_read` magic device_id branches → ops hooks where possible.
3. Contract tests for every debug_bin that reads `/proc` or `/sys`.
