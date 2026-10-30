# IR0 Tooling and Build

This document describes the active build, configuration, and validation flow.
It is aligned with the current root Makefile and menuconfig behavior.

## Core Build Targets

- `make ir0`: build the default artifact for `ARCH`/`BOARD`.
  - x86-64 → `kernel-x64.iso`
  - `arm64` → `kernel-arm64.bin`
  - `arm64 rpi4` → `build/arm64-rpi4/kernel8.img`
- `make ir0 arm64`: ARM64 QEMU `virt` aggregate.
- `make kernel-x64.bin`: build and link the kernel only.
- `make kernel-x64.iso`: generate an ISO from the current kernel image.
- `make all`: same artifact as `ir0`, without the `-Werror` policy.
- `make clean`: remove generated build artifacts.

## Runtime Targets

- `make run`: QEMU GUI on x86-64, serial on ARM64.
- `make run console`: serial/console focused execution (`-nographic`).
- `make run debug`: guest-error/int logging plus QEMU monitor.
- `make run fullscreen`: GTK full screen.
- `make run gdb`: start QEMU waiting for GDB on localhost:1234.
- `make run arm64 rpi4`: boot `build/arm64-rpi4/kernel8.img`.

`run` never builds. It resolves the artifact from `ARCH`/`BOARD` and refuses to
start when it is missing. `DISK=…` attaches a userspace image; `DISK=none` opts
out. See `Documentation/MAKEFILE.md` for the full variable list.

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

- `make ctr`: fast merge gate (kernel + `arch-guard` + `build-matrix-min` + host tests).
- `make agent-fast`: `ctr` plus the in-kernel ktest battery.
- `make ktest` / `make kernel-tests`: in-kernel tests in QEMU.
- `make ktm-check`: host tests + KTM classify/manifest/adoption/portability guards.
- `make build-matrix-min`: fast `.config` matrix; restores the caller's `.config`.
- `make health`: `kernel-analyze` + text budget + memsafe + kernel tests.
- `make arch-guard`: architecture boundary checks (including `fs/` no direct `<arch/`, no
  `interrupt/arch` in portable trees; see `scripts/architecture_guard.py`).
- `make repo-hygiene-guard`: repository hygiene checks.
- `python3 scripts/kernel_export_digest.py kernel-x64.bin`: SHA-256 of sorted global `nm`
  exports (same-toolchain rebuild comparison); see `Documentation/DECOUPLING.md`.

There is no `build-matrix-full`, QA smoke catalog, or release bundle in the
current Makefile. Historical target names in release documents are not
build-system entry points.

## Documentation Targets

| Target | What it does |
|--------|----------------|
| `make mandocs` | Language prompt, then all chapters or custom selection; installs |
| `make mandocs-en` | English prompts; installs to `~/.local/share/man/man7/` (no sudo) |
| `make mandocs-es` | Spanish prompts; `man IR0-krnl-es` after install |

System-wide: `sudo MANDOC_PREFIX=/usr/local make mandocs-en`

Uninstall: `make mandocs-uninstall` or `MANDOC_LANG=all make mandocs-uninstall`

- `make ai-dev-rules-install`: copy AI dev rules into gitignored `.cursor/`.

## External userspace tooling

The kernel Makefile does not expose ISD, kernel-manager, or machine-catalog
targets. `scripts/kernel_manager.py` remains in the tree for manual catalog
inspection, but it is not wired to `make`. Build userspace images with the
sibling [ISD](https://github.com/IRodriguez13/ISD) repository and attach the
result with `make run DISK=…`.

**Build `#N` vs release tag:** `#363` in host tooling is the **machine-local**
counter in `.build_number` (incremented on each `kernel-x64.bin` link). It is
independent of `IR0_VERSION_STRING` (`0.0.1-rc5`, etc.). When the release string
changes (e.g. rc5 → rc6), the Makefile resets `.build_number` to `1`
automatically (`.build_number_version` stamp).

## Current Strengths

- Strong config reproducibility via defconfig and scripted overrides.
- Useful blend of compile-time matrix and in-kernel QEMU tests.
- Menuconfig supports both TUI language modes and CLI automation.
- Small, predictable target set; no ISD or product coupling in the kernel build.

## Current Risks

- Runtime validation is still scenario-based, not exhaustive.
- Some advanced features are guarded by MVP-level policy.
- CI-style coverage is **local** (`make health`, `make ctr`, `make agent-fast`).
  GitHub Actions no longer runs the heavy `Tests` workflow; optional `Update LOC`
  is manual only.
