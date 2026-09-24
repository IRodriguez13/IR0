<!-- IR0 AI dev rule: ir0-arm64-rpi5-x86-regression-gate -->
<!-- alwaysApply: true -->
<!-- description: ARM64/RPi5 development must preserve portable facades and keep x86_64 green -->

# ARM64/RPi5 With Mandatory x86_64 Regression Gates

ARM64 and Raspberry Pi 5 are the primary post-0.0.1 target. Implement the kernel,
platform, driver, boot, and build support needed to reach common-kernel userspace.

## Architecture contract

- Keep ISA instructions, exception frames, translation-table formats, registers,
  interrupt-controller details, and firmware/platform boot code under `arch/arm64/`
  or an ARM64-specific driver backend.
- Common `kernel/`, `mm/`, `fs/`, `net/`, and scheduler code may use only neutral
  contracts and `includes/ir0/*` facades. Extend callbacks/ops tables when needed.
- Do not add ARM conditionals to common hot paths when build-selected backends or
  facade callbacks can express the boundary.
- Never weaken, stub, or special-case x86_64 merely to make ARM64 advance.
- Compare Linux arm64/RPi implementations for behavior and invariants, while
  adapting them to IR0 interfaces and licensing constraints.

## Mandatory gates for every ARM64 increment

```bash
make -s smoke-arm64
make -s arm64-rpi5-compile
make -s arch-guard
make -s isa-security-guard
make -s build-matrix-min
make -s kernel-x64.bin
make -s -C tests/host run
```

Use `make -s pre-submit SUBSYSTEM=arm64` as the aggregate local gate when it
covers the touched area. A RPi5 compile stub proves only board selection and
linkage; it is never evidence of hardware boot or common-kernel userspace.

If boot, MM, exceptions, signals, syscalls, scheduling, or shared drivers changed,
also run the x86_64 QEMU product boot gate before commit. ARM64 success is not enough:
an increment is complete only when its ARM probe passes and the x86_64 baseline still
builds and boots without new panic, PF, GPF, corruption, or ABI regression.

Record unsupported ARM64 stages as explicit `SKIP`/blocked results. Never report a
scaffold, link-only artifact, or early harness as a full common-kernel boot.
