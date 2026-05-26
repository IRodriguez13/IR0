<!-- IR0 AI dev rule: kernel-c-allman-style -->
<!-- alwaysApply: false -->
<!-- description: Enforce Allman brace style and kernel source hygiene for C/C headers -->

# IR0 C/Headers Style (Allman)

Apply these conventions when editing kernel C and header files.

## Brace Style

- Use Allman braces consistently:

```c
if (condition)
{
    do_work();
}
```

- Do not use K&R same-line opening braces for control blocks or function bodies.

## Header Hygiene

- Keep SPDX at the top of source/header files.
- Preserve module/file header blocks and keep them aligned with project style.
- Keep comments concise and in English.

## Architectural Safety

- Do not introduce direct `#include <drivers/...>` in high-level layers where facades are expected.
- Prefer `includes/ir0/*` facade headers.
