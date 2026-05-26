# IR0 Userspace Bootstrap

| Field | Value |
|-------|-------|
| Version | 0.1 |
| IR0 phase | T1–T2 |
| Status | stable |
| Depends on | boot, process, vfs, tty |
| Man page | IR0-userspace (section 7) |
| Primary sources | `setup/pid1/irinit.c`, `kernel/main.c`, `kernel/rootfs_base.c`, `scripts/inject_init_minix.py`, `Makefile` |

## 1. Overview

Production boot loads **`/sbin/init`** via `kexecve` from `kmain` when
`KERNEL_DEBUG_SHELL=0`. The reference PID1 implementation is **irinit**
(`setup/pid1/irinit.c`), built static with musl and injected into the MINIX root
image. BusyBox, TCC, and DoomGeneric are optional rootfs payloads for smoke and
T2 graphics milestones.

## 2. Internal architecture

| Artifact | Role |
|----------|------|
| `irinit.c` | PID1: console attach, spawn `/bin/sh`, reap zombies |
| `init_musl.c` | musl syscall smoke binary |
| `rootfs_base.c` | Creates `/bin`, `/sbin`, `/dev`, `/proc`, … on disk |
| `inject_init_minix.py` | Writes binaries into MINIX v1 image |
| `busybox-1.36.1` | Third-party applets; configs in `setup/busybox/` |
| `kernel-x64-userspace.bin` | Kernel built with `IR0_USERSPACE_INIT_BOOT=1` |

**irinit behavior (non-smoke):** prepare environment → attach console → spawn shell →
respawn on exit; limits on consecutive SEGV and empty shell loops.

## 3. Data flow

```text
  make kernel-x64-userspace.iso + disk.img
       │
       ▼
  inject_init_minix.py  (irinit → /sbin/init, busybox → /bin/...)
       │
       ▼
  QEMU: kernel-x64-userspace.iso + disk.img
       │
       ▼
  kmain → vfs_init_root (MINIX /)
       → ir0_rootfs_prepare_userspace_base()  (mkdir layout)
       → kexecve("/sbin/init")
       │
       ▼
  irinit → open /dev/console → spawn /bin/sh (BusyBox ash)
       │
       ▼
  user: doom, tcc, coreutils smokes
```

ASCII:

```text
  [MINIX disk]          [kernel ISO]
  /sbin/init=irinit  +  userspace boot flag
         │                    │
         └────────┬───────────┘
                  ▼
            kexecve("/sbin/init")
                  ▼
              irinit → /bin/sh
```

## 4. Responsibilities

- Kernel: mount root, ensure base dirs, exec init once, then schedule.
- irinit: reap zombies, respawn shell, no driver access (syscalls only).
- Build system: musl cross compiler (`MUSL_CC`), inject scripts, ISO targets.

## 5. Subsystem boundaries

- PID1 must not link kernel symbols; static musl only.
- `debug_bins` shell replaces init when `KERNEL_DEBUG_SHELL=1` — separate T0 path.
- Phase-named inits under `setup/pid1/init_fase*.c` are smoke harnesses, not production PID1.

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|---------------|
| Boot | `kexecve` handoff |
| VFS | MINIX root, tmpfs fallback if disk missing |
| TTY | irinit uses `/dev/console`, termios |
| Process | fork/exec/wait in shell and applets |
| T2 | DoomGeneric via `/usr/share/doom` layout in rootfs_base |

## 7. Visual maps

```text
  build chain:
  musl-gcc → irinit/busybox/doom binaries
       → inject_init_minix.py → disk.img
       → make kernel-x64-userspace.iso
       → QEMU smoke targets
```

## 8. Important invariants

1. `/sbin/init` must exist on root FS for production boot path.
2. `IR0_USERSPACE_INIT_BOOT=1` required on kernel for real init (not debug shell).
3. irinit smoke mode (`DIRINIT_SMOKE`) halts after probes — not interactive default.
4. musl requires `x86_64-linux-musl-gcc` or `musl-gcc`.

## 9. Debugging tips

| Symptom | Fix |
|---------|-----|
| `musl cross compiler not found` | `apt install musl-tools`, set `MUSL_CC` |
| `/sbin/init` not found | Re-run inject; check disk.img |
| Kernel shell instead of init | Wrong ISO (use userspace variant) |
| ash silent | See IR0-tty; verify `/dev/console` |

Targets: `make build-irinit`, `make smoke-userspace-shell`, `make run-irinit-interactive-gui`.

See `SETUP.md` for full bootstrap flow.

## 10. Future roadmap

- runit-style service supervision — irinit is minimal, not full runit port.
- musl dynamic linking — static binaries only today.
- Proper package layout / shared libs on rootfs — partial `/lib` staging.
- systemd-style init — **out of scope** for IR0 T1.

Phase doc: `Documentation/fase58e-ash-interactive-console.md`.
