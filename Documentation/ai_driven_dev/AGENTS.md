# IR0 Agent Entry Point

Read `Documentation/ai_driven_dev/rules-digest.md` at session start: it
summarizes every rule under `Documentation/ai_driven_dev/rules/`. Open a full
rule (and `Documentation/ai_driven_dev/README.md`) only when the task touches
its topic, and always before changing the kernel area it governs.

Every kernel bug fix follows `ir0-bugfix-red-green-proof.md`: reproduce the
failure on the unpatched tree, then prove the same test passes with the fix. A
fix without red/green evidence is not accepted.

ARM64 and Raspberry Pi 5 are the primary post-0.0.1 development scope. Follow
`ir0-arm64-rpi5-x86-regression-gate.md`: keep ISA details behind architecture
facades, prove each ARM increment honestly, and preserve the x86_64 baseline.

Use `Documentation/ai_driven_dev/skills/ctr/SKILL.md` for completion gates. Do
not patch third-party userspace to hide missing Linux ABI behavior in IR0.
