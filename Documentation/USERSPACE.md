# Coupling IR0 (kernel) ↔ ISD

> **Last verified:** 2026-07-28  
> **Source of truth:** this file, `scripts/make/isd.mk`, `scripts/bootstrap-isd.sh`, sibling [ISD](https://github.com/IRodriguez13/ISD), [SETUP.md](../SETUP.md).  
> **Spanish:** [`esp/USERSPACE.md`](esp/USERSPACE.md)

## Why two repositories?

The kernel alone is not a Unix distro. Product PID1 (**runit**), **BusyBox**,
login/doas, packages, man pages, and the finished MINIX `disk.img` live in
**ISD** (IR0 Software Distribution). IR0 compiles the kernel, exports UAPI,
delegates the ISD build, and boots the ISD-owned image.

| Lives in **IR0** | Lives in **ISD** |
|------------------|------------------|
| Kernel, drivers, UAPI (`includes/uapi/`) | Packages, services, rootfs, profiles |
| Boot ISO / QEMU orchestration | `out/<arch>/images/<profile>/disk.img` |
| Host-deps / first-boot wrapper | `.isdconfig` extras, `stage-rootfs`, `pack-minix` |

**Hard rule:** IR0 must not inject BusyBox/runit/nano one-by-one on the
canonical product path. Legacy `load-userspace-runit` remains for smokes
(`IR0_LEGACY_USERSPACE=1`).

## Fastest path (first time)

```bash
git clone https://github.com/IRodriguez13/IR0.git
cd IR0
make first-boot PROFILE=minimal
make run PROFILE=minimal
```

`first-boot` will:

1. Check host deps (`IR0_DEPS_INSTALL=ask|yes|never`, default **ask**)
2. Ask before any `sudo apt-get install` (never captures the password)
3. Clone `../ISD` if missing
4. Write `.isdconfig` only if absent
5. Fetch sources, export UAPI, build packages (stamp-incremental)
6. Stage rootfs + pack `disk.img` under ISD
7. Build `kernel-x64-userspace.iso`

Layout:

```text
parent/
├── IR0/
└── ISD/
```

| Variable | Default | Role |
|----------|---------|------|
| `IR0_ISD_ROOT` | `../ISD` | distribution tree |
| `IR0_ISD_URL` | `https://github.com/IRodriguez13/ISD.git` | clone URL |
| `PROFILE` | `minimal` (when passed on CLI) | ISD product profile |
| `IR0_PRODUCT_PROFILE` | alias of ISD profile | compat |
| `ISD_ARCH` | `x86_64` | userspace arch name |
| `IR0_USERSPACE_ROOT` / `_URL` | aliases of `IR0_ISD_*` | **deprecated** |

### Config layers

| File | Owner | Meaning |
|------|-------|---------|
| `IR0/.config` | kernel | Kconfig |
| `ISD/profiles/<p>/profile.conf` | ISD | login/root/fsck policy |
| `ISD/profiles/<p>/packages.txt` | ISD | mandatory packages |
| `ISD/.isdconfig` | ISD | optional extras (`make isdconfig`) |

### Targets

| Target | Role |
|--------|------|
| `make first-boot PROFILE=…` | Full product bootstrap |
| `make isdconfig PROFILE=…` | Toggle extras |
| `make isd` / `isd-rootfs` / `isd-image` | Delegate to ISD |
| `make run PROFILE=…` | Boot ISD disk (no rebuild if up to date) |
| `make bootstrap-userspace` | **Deprecated** → `first-boot` |
| `IR0_LEGACY_USERSPACE=1 make run` | Old inject path (smokes) |

### Incremental builds

ISD stamps under `out/<arch>/stamps/{toolchain,uapi,packages,rootfs,images}/`.
A second `make first-boot PROFILE=minimal` without input changes must not
re-run package `build.sh` scripts.

See ISD [`Documentation/PACKAGES.md`](https://github.com/IRodriguez13/ISD/blob/master/Documentation/PACKAGES.md).
