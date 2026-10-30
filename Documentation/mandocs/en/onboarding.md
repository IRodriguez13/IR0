# IR0 Onboarding — First clone to first patch

| Field | Value |
|-------|-------|
| Version | 0.2 |
| IR0 phase | T0–T1 |
| Status | stable |
| Depends on | boot, multi-arch |
| Man page | IR0-onboarding (section 7) |
| Primary sources | `README.md`, `SETUP.md`, `CONTRIBUTING.md`, `scripts/deptest.sh`, `Makefile`, `scripts/run_qemu.sh`, `Documentation/USERSPACE.md` |

## 1. Overview

This chapter is for **new contributors**. It covers cloning IR0, checking the
host, building the kernel, running it in QEMU, reading man pages, and landing a
first small change.

Out of scope: rewriting the kernel, ARM appliance flash, or claiming full
Linux ABI. Subsystem depth lives in other `IR0-*` pages. Userspace images are
built by the sibling **ISD** repository, not by this Makefile.

## 2. Internal architecture (entry surface)

| Piece | Role |
|-------|------|
| `make check-env` / `deptest` | Host diagnostic (required / optional / unusable) |
| `make defconfig` / `ir0` / `run` | Official first boot (x86-64 QEMU) |
| `make menuconfig` | Curses configuration TUI (English / Spanish variants) |
| `make sync-mandocs` / `make man TOPIC=` | Docs without MANPATH fights |
| `make ctr` | Fast merge gate → `CTR_OK` |
| `make agent-fast` | `ctr` plus the in-kernel ktest battery |
| `CONFIG_BOOT_LOG_HOSTSHARE` | Opt-in dump of the log ring to a 9p `ir0-boot.log` |

`make run` only launches an artifact that is already on disk. It never builds
and never invokes ISD.

## 3. Data flow — first boot

```text
  git clone IR0 → cd IR0
       → make check-env
       → make defconfig
       → make ir0        # kernel-x64.iso
       → make run        # QEMU → kernel banner → /sbin/init handoff
```

`make ir0` builds a kernel-only ISO. A shell needs a userspace image built by
the sibling **ISD** repository; attach it with `make run DISK=…`. Without
`/sbin/init` the kernel panics at handoff (there is no in-kernel shell). See
`Documentation/USERSPACE.md`.

ARM64 builds use positional goals:

```bash
make ir0 arm64
make run arm64
make ir0 arm64 rpi4
make run arm64 rpi4
```

### Optional boot log on the host

Enable `CONFIG_BOOT_LOG_HOSTSHARE=y` in `make menuconfig`, then attach a 9p
share tagged `ir0share`:

```bash
make run IR0_QEMU_EXTRA="-fsdev local,id=ir0fs,path=$PWD,security_model=none \
  -device virtio-9p-pci,fsdev=ir0fs,mount_tag=ir0share,disable-modern=on"
```

The guest writes `ir0-boot.log` into the shared host directory and emits the
serial tag `BOOT_LOG_HOSTSHARE_OK`.

## 4. Responsibilities

- Contributor: run `check-env` before reporting “build broken”.
- Maintainer paths: keep the root Makefile thin; recipes live in
  `scripts/make/*.mk`.
- Kernel: never panic if 9p is absent; emit a SKIP tag when opt-in but no
  device.

## 5. Subsystem boundaries

- Do not add agent one-shot smokes to the root Makefile.
- Do not claim Raspberry Pi images are SD-flashable (lab UART / stub only).
- Mandocs document **implemented** behavior; aspirational items only in §10.

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|-------------|
| IR0-boot | Boot pipeline / serial banner contract |
| IR0-multi-arch | ARM lab ceilings |
| filesystems / virtio-9p | Host share used by the boot-log dump |
| CONTRIBUTING.md | Style, commit, CTR gates |

## 7. Visual maps

```text
  New contributor
       │
       ├─► check-env ──► defconfig ──► ir0 ──► run
       │                                      │
       │                                      └─► (optional) DISK=… / 9p boot log
       │
       └─► sync-mandocs ──► man TOPIC=onboarding|boot|mm|…
                              └─► first bug ──► ctr
```

## 8. Important invariants

1. Official first boot = **desktop x86-64 under QEMU**.
2. ARM64 boards are lab targets; `rpi5` remains a compile stub.
3. Boot-log hostshare is **opt-in** (`CONFIG_BOOT_LOG_HOSTSHARE`); `make run`
   stays clean by default.
4. Facade headers under `includes/ir0/` are **not** all covered by mandocs — see
   INDEX.

## 9. Debugging tips — first bug walkthrough

1. Boot with `make run` (or `make run console`) and note the serial banner
   `IR0 kernel …` (portable banner; then ARCH / HYPERVISOR / PLATFORM).
2. `make man TOPIC=boot` — find `ir0_boot_serial_ready` /
   `includes/ir0/boot_log.h`.
3. Grep the tree for that symbol; open the C file; read one call site in
   `kmain`.
4. Make a **one-file** doc or comment fix, then run the fast gate:

```bash
make ctr
# expect CTR_OK
```

Add the in-kernel battery when the change touches QEMU/ktest paths:

```bash
make agent-fast
```

## 10. Guest manuals

Host manuals are installed by this tree:

```bash
make sync-mandocs
make man TOPIC=onboarding
```

Guest man pages (`/usr/share/man/cat7/`) belong to the **ISD** rootfs build,
not to the kernel Makefile. IR0 no longer exposes `prepare-guest-mandocs`,
`first-boot`, or `load-userspace-runit` targets.

## 11. Future roadmap

- More “first bug” recipes per subsystem (mm, vfs, net).
- Broader mandoc coverage of `includes/ir0/` (honest backlog — not done).
