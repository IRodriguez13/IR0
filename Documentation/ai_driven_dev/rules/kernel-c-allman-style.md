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
- Includes, then macros, then struct/typedef/enum definitions go at the top
  of the file. No `#include` in the middle of a file, and no duplicate
  include.
- Include what you use, nothing more. A split must not copy the original
  file's include block into every new file: run
  `python3 scripts/include_prune.py <files.c>` on each new or split file.
  It keeps an include only if dropping it changes the stripped object on
  x86_64 or arm64, and it keeps `<config.h>` in files with conditionals.
- One declaration per symbol: a private header includes its
  `includes/ir0/*` facade instead of copying prototypes or types, and a `.c`
  file never re-declares a prototype that a header owns (arch-guard
  `duplicate-declaration`).

## Architectural Safety

- Do not introduce direct `#include <drivers/...>` in high-level layers where facades are expected.
- Prefer `includes/ir0/*` facade headers.
