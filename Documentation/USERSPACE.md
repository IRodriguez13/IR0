# Coupling IR0 (kernel) ↔ IR0-userspace

> **Last verified:** 2026-07-25  
> **Source of truth:** this file, `Makefile` (`IR0_USERSPACE_ROOT`, `check-userspace`, `headers_install`), sibling [IR0-userspace](https://github.com/IRodriguez13/IR0-userspace), and [TREE_CONTRACT](../../IR0-desktop/Documentation/TREE_CONTRACT.md) when present.

## Boundary

| Lives in **IR0** | Lives in **IR0-userspace** |
|------------------|----------------------------|
| Kernel, drivers, UAPI (`includes/uapi/`) | runit, BusyBox, login/doas, product `/etc` |
| Kernel test fixtures (`setup/pid1/`) | Package recipes, services, rootfs profiles |
| Orchestration Make targets that *delegate* | Built ELFs and `disk.img` under `out/` |

**Hard rule:** if PID 1 can replace it without recompiling the kernel, it does not live in IR0.

Product boot always `kexecve("/sbin/init")`. There is no in-kernel debug shell or `debug_bins/` ring-0 command harness.

## Suggested layout

```text
parent/
├── IR0/                 # this tree
├── IR0-userspace/       # https://github.com/IRodriguez13/IR0-userspace
└── IR0-desktop/         # optional desktop product + DESK smokes
```

Override paths if needed:

```bash
export IR0_USERSPACE_ROOT=/path/to/IR0-userspace
export IR0_ROOT=/path/to/IR0          # from the userspace tree
```

## Wire-up (kernel side)

```bash
# 1. Clone sibling next to IR0 (or set IR0_USERSPACE_ROOT)
git clone https://github.com/IRodriguez13/IR0-userspace.git ../IR0-userspace

# 2. Export public ABI for userspace builds
make headers_install DESTDIR=../IR0-userspace/out/sysroot

# 3. Build / install product rootfs (delegates to sibling)
make check-userspace
make build-runit
make load-userspace-runit

# 4. Boot product path
make kernel-x64-userspace.iso
# or: make run-pid1 / smoke-runit-boot
```

`make check-userspace` **fails** if the sibling is missing (never silent skip-as-PASS).

## Wire-up (userspace side)

From `IR0-userspace`:

```bash
export IR0_ROOT=../IR0
make fetch build rootfs
# optional: make image   # needs a built kernel ISO from IR0_ROOT
```

See `IR0-userspace/README.md` and `userspace/README.md` in this tree (pointer only).

## What this tree must not re-add

- BusyBox / runit / doas sources or product `/etc` under `setup/`
- In-kernel mono shell (`dbgshell`) or `debug_bins/` command registry
- `#include` of product userspace into the kernel link
