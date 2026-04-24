# IR0 Tooling and Build

This document describes the active build, configuration, and validation flow.
It is aligned with the current Makefile and menuconfig behavior.

## Core Build Targets

- `make -j4`: full kernel build, link, and ISO generation.
- `make kernel-x64.bin`: build and link kernel only.
- `make kernel-x64.iso`: generate ISO from the current kernel image.
- `make clean`: remove generated build artifacts.

## Runtime Targets

- `make run`: QEMU GUI profile with standard IR0 emulated hardware.
- `make run-console`: serial/console focused execution.
- `make run-debug`: debug-oriented runtime profile.
- `make run-gdb`: starts QEMU waiting for GDB on localhost.

## Configuration Workflow

- `make defconfig`: reset to baseline config.
- `make menuconfig`: interactive TUI configuration.
- `make menuconfig-en`: force English menuconfig session.
- `make menuconfig-es`: force Spanish menuconfig session.
- `python3 scripts/kconfig/menuconfig.py --set ...`: non-interactive mutation.
- `python3 scripts/kconfig/menuconfig.py --preset ...`: preset-based setup.

### Key Config Areas

- Driver init selection (`CONFIG_INIT_*`).
- Filesystem selection (`CONFIG_ENABLE_FS_*`).
- Scheduler policy (`CONFIG_SCHEDULER_POLICY`).
- Keyboard layout default (`CONFIG_KEYBOARD_LAYOUT`).
- Menu language (`CONFIG_TOOL_MENUCONFIG_LANG`).

## Validation Targets

- `make build-matrix-min`: fast profile matrix.
- `make build-matrix-full`: extended matrix with guards.
- `make runtime-net-check`: QEMU runtime smoke for networking paths.
- `make scale-readiness-gate`: stabilization gate target.
- `make arch-guard`: architecture boundary checks.

## Host Test Harness

- `make -C tests/host`: compile host-side tests.
- `make -C tests/host run`: execute host-side test suite.

## Current Strengths

- Strong config reproducibility via defconfig and scripted overrides.
- Useful blend of compile-time matrix and runtime QEMU smoke checks.
- Menuconfig supports both TUI language modes and CLI automation.

## Current Risks

- Runtime validation is still scenario-based, not exhaustive.
- Some advanced features are guarded by MVP-level policy.
- CI-style coverage depends on local execution discipline.

