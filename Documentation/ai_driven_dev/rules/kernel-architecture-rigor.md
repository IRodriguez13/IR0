<!-- IR0 AI dev rule: kernel-architecture-rigor -->
<!-- alwaysApply: true -->
<!-- description: Enforce IR0 kernel architecture rigor, style, testing, and documentation standards -->

# IR0 Kernel Architecture Rigor

Apply these rules in every kernel-related task.
These rules apply to the entire kernel codebase, including (non-exhaustive):
`kernel/*`, `fs/*`, `drivers/*`, `arch/*`, `includes/*`, `debug_bins/*`,
build/config wiring (`Makefile`, `setup/Kconfig`, `setup/defconfig`,
`scripts/kconfig/*`), and technical kernel documentation.

## Architecture and Design

- Enforce the architectural criteria defined in `Documentation/ai_driven_dev/skills/ctr/SKILL.md`.
- Preserve subsystem decoupling through `includes/ir0/*` facades and stable interfaces.
- Prefer extending interfaces/facades over adding direct coupling to low-level modules.
- Keep scheduler/filesystem/driver selection config-driven and consistent across Kconfig, defconfig, menuconfig tooling, and Makefile wiring.

## Coding Style and Hygiene

- Use Allman brace style for C code changes.
- Keep SPDX and file/module header blocks present and consistent in kernel source/header files.
- Do not version unnecessary artifacts (temporary logs, generated noise, accidental local outputs).
- Do not hardcode runtime-obtainable values when the kernel can read/derive them from hardware,
  protocol metadata, filesystem state, configuration, or syscall-visible kernel state.
- For `/proc`, `/sys`, `/dev`, and debug diagnostics, prefer real runtime data over placeholder constants.

## Testing and Execution Discipline

- Run compile and matrix checks after substantial changes (at minimum kernel build + architecture guardrails).
- For MM, paging, exec, syscall, or context-switch bugs, cross-check invariants against major OSS kernels per `Documentation/ai_driven_dev/rules/oss-kernel-reference.md`.
- For multi-agent work and progress reports, follow `Documentation/ai_driven_dev/rules/ir0-development-multiagent-format.md`.
- For broad or cross-subsystem work, use Plan mode per `Documentation/ai_driven_dev/rules/ir0-development-plan-mode.md` before implementing.
- Prioritize regressions, race risks, ABI/config breakage, and integration consistency during reviews.

## Documentation and Comments

- Keep technical docs and code comments in English.
- Spanish documentation is allowed only as a parallel version under `Documentation/esp/`.
- Ensure docs reflect real implemented behavior (no aspirational/stub claims as completed).
