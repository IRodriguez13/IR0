# Userspace coupling (pointer only)

This directory is **not** a product rootfs. Unix userspace lives in the sibling repository:

**https://github.com/IRodriguez13/IR0-userspace**

| Env / target | Role |
|--------------|------|
| `IR0_USERSPACE_ROOT` | Path to the sibling (default: `../IR0-userspace`) |
| `make check-userspace` | Fail if sibling missing |
| `make headers_install` | Export `includes/uapi/` for userspace builds |
| `make build-runit` / `load-userspace-runit` | Delegate build/install to the sibling |

Full instructions: [`Documentation/USERSPACE.md`](../Documentation/USERSPACE.md).
