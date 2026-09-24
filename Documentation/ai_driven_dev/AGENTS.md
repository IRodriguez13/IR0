# IR0 Agent Entry Point

Read `Documentation/ai_driven_dev/README.md` and all always-apply rules under
`Documentation/ai_driven_dev/rules/` before changing the kernel.

ARM64 and Raspberry Pi 5 are the primary post-0.0.1 development scope. Follow
`ir0-arm64-rpi5-x86-regression-gate.md`: keep ISA details behind architecture
facades, prove each ARM increment honestly, and preserve the x86_64 baseline.

Use `Documentation/ai_driven_dev/skills/ctr/SKILL.md` for completion gates. Do
not patch third-party userspace to hide missing Linux ABI behavior in IR0.
