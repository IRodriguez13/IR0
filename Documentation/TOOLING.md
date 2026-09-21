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

- **`CONFIG_ENABLE_USB_HOST`** (Makefile / `autoconf.h`): gates compilation of the USB host PCI scan scaffold in `drivers/usb/usb_host.c`. When unset or `n`, `ir0_usb_host_init` is a no-op and `ir0_usb_host_controller_count` stays zero.
- **`IR0_ENABLE_USB`** (`setup/kernel_config.h`): target-profile macro (Desktop/Server/IoT/…) that enables the higher-level USB *driver class* bundle (`IR0_ENABLE_USB_DRIVER`, storage, HID). It does not by itself pull in the host scaffold; use **`CONFIG_ENABLE_USB_HOST`** when you want the kernel to compile and optionally run the host PCI discovery path (`CONFIG_INIT_USB_HOST` controls early init registration).

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
- `make arch-guard`: architecture boundary checks (including `fs/` no direct `<arch/`, no
  `interrupt/arch` in portable trees; see `scripts/architecture_guard.py`).
- `python3 scripts/kernel_export_digest.py kernel-x64.bin`: SHA-256 of sorted global `nm`
  exports (same-toolchain rebuild comparison); see `Documentation/DECOUPLING.md`.


## Documentation Targets

| Target | What it does |
|--------|----------------|
| `make mandocs` | Language prompt, then all chapters or custom selection; installs |
| `make mandocs-en` | English prompts; installs to `~/.local/share/man/man7/` (no sudo) |
| `make mandocs-es` | Spanish prompts; `man IR0-krnl-es` after install |

System-wide: `sudo MANDOC_PREFIX=/usr/local make mandocs-en`

Uninstall: `make mandocs-uninstall` or `MANDOC_LANG=all make mandocs-uninstall`

- `make ai-dev-rules-install`: copy AI dev rules into gitignored `.cursor/`.

## Persistent machine tooling

- `make kmang`: TUI catalog for boot ISOs under `IR0-machines/` (Default /
  Fallback / Workspace). Build `#N` is host-local; compare with SHA-256.
  On **desktop** / **desktop-console** profiles, **Enter** (select kernel) and
  **`b`** (boot Default) ask **terminal only** vs **X direct**, then write a
  one-shot `/etc/ir0-session` on the machine disk before `poweron`.
- `make kernel-manager-install`: rebuild workspace ISO and enroll it as Default.
- `make poweron`: boot Default (or workspace ISO only if the catalog is empty).
- `make machine-update-kernel`: rebuild ISO only — does not enroll; kmang `i`
  is still required before `poweron` picks it up.
- `make usmang`: host inspector for ISD release, package origins, `USERLAND_BASE`,
  and desktop package set (independent of kernel `#N`).
- `make kmang-test`: host regression tests for the kernel catalog, confirm-twice cleanup, and session prompt.
- `make usmang-test`: host tests for the ISD inspector CLI and help legend.
- `make smoke-desktop-twm-resize`: twm title resize → xterm SIGWINCH + session survival (PROFILE=desktop).

**Build `#N` vs release tag:** `#363` in kmang is the **machine-local** counter in
`.build_number` (incremented on each `kernel-x64.bin` link). It is independent of
`IR0_VERSION_STRING` (`0.0.1-rc5`, etc.). When the release string changes (e.g.
rc5 → rc6), the Makefile resets `.build_number` to `1` automatically
(`.build_number_version` stamp).

## Current Strengths

- Strong config reproducibility via defconfig and scripted overrides.
- Useful blend of compile-time matrix and runtime QEMU smoke checks.
- Menuconfig supports both TUI language modes and CLI automation.

## Current Risks

- Runtime validation is still scenario-based, not exhaustive.
- Some advanced features are guarded by MVP-level policy.
- CI-style coverage is **local** (`make health`, CTR gates, KTM/smokes). GitHub Actions
  no longer runs the heavy `Tests` workflow; optional `Update LOC` is manual only.

