# KTM / tier smoke PID1 harness (not product rootfs)

This directory holds **test init binaries and staging ELFs** for kernel QA (KTM,
tier smokes). It is **not** the product PID1.

| Concern | Location |
|---------|----------|
| Product PID1 (runit) | `ISD/services/`, staged to `/sbin/init` on `disk.img` |
| Kernel Kconfig | `setup/Kconfig`, `setup/defconfig` |
| KTM smoke inits | `setup/pid1/*.c` (sources kept; no root Makefile target) |

## Expected contents

- `init_musl.c`, `ktm_*.c`, `init_*.c` — ring-3 smoke binaries
- `fase*_staging/` — **legacy lab artifacts** (do not treat as product rootfs)
- `fase50_busybox_real` — legacy inject smoke only (`IR0_LEGACY_USERSPACE`)

## Usage (smokes)

These harnesses are **not wired into the lean root Makefile**. Compile them with
the host toolchain or a dedicated ISD/QA command when you need one:

```bash
gcc -static -o setup/pid1/init setup/pid1/init_musl.c    # example only
```

Product boot: build the rootfs with ISD and run `make run DISK=…` (see
`Documentation/USERSPACE.md`). The old `build-ktm-*`, `load-userspace-runit`,
and `first-boot` targets were removed.

Future move (planned): `setup/pid1/` → `qa/smokes/pid1/` per
`Documentation/TREE_REORGANIZATION_PLAN.md`.
