# IR0 and Unix-Like Semantics

This document captures practical compatibility boundaries, not historical claims.

## Where IR0 Aligns Well

- Unix-like syscall model for core file/process operations.
- VFS and path-based interaction model with pseudo-filesystem introspection.
- Process ownership and permission fields (`uid/gid/euid/egid`, `umask`).
- Shell-style userland/debug-binary workflow over syscall-only interfaces.

## Where IR0 Intentionally Differs

- Debug-first architecture with integrated kernel debug shell workflows.
- Kconfig-driven subsystem gating exposed directly to build/runtime strategy.
- Staged driver bootstrap and registry model tailored for kernel bring-up control.
- Lightweight account/sudo path (MVP-level) rather than full user database stack.

## Current Compatibility Limits

- Security/account model is not a full production Unix auth stack.
- Scheduler depth is still stabilization-first.
- Some advanced POSIX edge cases are partial or evolving.

## Strengths

- Strong architectural modularity and subsystem decoupling.
- Fast iteration loop through tooling plus runtime observability.

## Weak Points

- Some compatibility features are present as minimum viable implementations.
- Long-tail behavior still requires broader real-world regression scenarios.