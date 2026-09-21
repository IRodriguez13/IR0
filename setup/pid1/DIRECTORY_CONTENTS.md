# KTM / tier smoke PID1 harness (not product rootfs)

This directory holds **test init binaries and staging ELFs** for kernel QA (KTM,
tier smokes, legacy phase targets). It is **not** the product PID1.

| Concern | Location |
|---------|----------|
| Product PID1 (runit) | `ISD/services/`, staged to `/sbin/init` on `disk.img` |
| Kernel Kconfig | `setup/Kconfig`, `setup/defconfig` |
| KTM smoke inits | `setup/pid1/*.c` → built by `scripts/make/testing.mk` |
| Legacy phase smokes | `setup/make/legacy-smokes.mk` (`IR0_LEGACY_SMOKE=1`) |

## Expected contents

- `init_musl.c`, `ktm_*.c`, `init_*.c` — ring-3 smoke binaries
- `fase*_staging/` — **legacy lab artifacts** (do not treat as product rootfs)
- `fase50_busybox_real` — legacy inject smoke only (`IR0_LEGACY_USERSPACE`)

## Usage (smokes)

```bash
make -s build-ktm-<scenario>-bin    # see scripts/make/testing.mk
IR0_LEGACY_USERSPACE=1 make load-userspace-runit   # legacy MINIX inject path
```

Product boot: `make first-boot PROFILE=…` → ISD-owned `disk.img` (see
`Documentation/USERSPACE.md`).

Future move (planned): `setup/pid1/` → `qa/smokes/pid1/` per
`Documentation/TREE_REORGANIZATION_PLAN.md`.
