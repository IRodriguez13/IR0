# Userspace coupling (pointer only)

This directory is **not** a product rootfs. Unix userspace lives in the sibling repository:

**https://github.com/IRodriguez13/IR0-userspace**

First time from the kernel tree:

```bash
make first-boot && make run
```

| Env / target | Role |
|--------------|------|
| `IR0_USERSPACE_ROOT` | Path to the sibling (default: `../IR0-userspace`) |
| `make first-boot` / `bootstrap-userspace` | Clone sibling if needed + minimal distro |
| `make check-userspace` | Fail if sibling missing |
| `make headers_install` | Export `includes/uapi/` for userspace builds |
| `make load-userspace-runit` | BusyBox + runit → `disk.img` |

Full instructions: [`Documentation/USERSPACE.md`](../Documentation/USERSPACE.md).
