# Userspace coupling (pointer only)

This directory is **not** a product rootfs. Unix userspace lives in the sibling
**ISD** (IR0 Software Distribution) repository:

**https://github.com/IRodriguez13/ISD**

Supported layout (no path heuristics):

```text
workspace/
├── IR0/
└── ISD/
```

The IR0 Makefile does not clone or build ISD. Build the kernel, build the
userspace image with ISD's own tooling, then attach it:

```bash
make ir0
make run DISK=../ISD/out/x86_64/images/minimal/disk.img
```

| Env / target | Role |
|--------------|------|
| `DISK=…` | Attach an ISD-built raw disk to `make run` |
| `make headers_install` | Export `includes/uapi/` for external userspace builds |
| `IR0_ISD_ROOT` | Conventional path to ISD (documentation/tooling only) |
| `IR0_USERSPACE_ROOT` | **Deprecated** alias of `IR0_ISD_ROOT` |

Full boundary: [`Documentation/USERSPACE.md`](../Documentation/USERSPACE.md).

Former name: **IR0-userspace** (renamed to ISD, same maintainer).
