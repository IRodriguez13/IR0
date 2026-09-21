# Userspace coupling (pointer only)

This directory is **not** a product rootfs. Unix userspace lives in the sibling **ISD**
(IR0 Software Distribution) repository:

**https://github.com/IRodriguez13/ISD**

Supported layout (no path heuristics):

```text
workspace/
├── IR0/
└── ISD/
```

First time from the kernel tree:

```bash
make first-boot PROFILE=minimal
make run PROFILE=minimal
```

| Env / target | Role |
|--------------|------|
| `IR0_ISD_ROOT` | Path to ISD (default: `../ISD`) |
| `IR0_USERSPACE_ROOT` | **Deprecated** alias of `IR0_ISD_ROOT` |
| `make first-boot` | Clone ISD if needed + build profile + ISO |
| `make check-isd` | Fail if ISD missing or interface mismatch |
| `make headers_install` | Export `includes/uapi/` for ISD builds |
| `make load-userspace-runit` | **Legacy** smoke inject (`IR0_LEGACY_USERSPACE=1`) |

Full instructions: [`Documentation/USERSPACE.md`](../Documentation/USERSPACE.md).

Former name: **IR0-userspace** (renamed to ISD, same maintainer).
