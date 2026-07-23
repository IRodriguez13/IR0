# IR0 Onboarding — First clone to first patch

| Field | Value |
|-------|-------|
| Version | 0.1 |
| IR0 phase | T0–T1 |
| Status | stable |
| Depends on | boot, multi-arch |
| Man page | IR0-onboarding (section 7) |
| Primary sources | `README.md`, `SETUP.md`, `CONTRIBUTING.md`, `scripts/deptest.sh`, `scripts/make/profiles.mk`, `scripts/make/hostshare-boot.mk`, `includes/ir0/boot_log_hostshare.h` |

## 1. Overview

This chapter is for **new contributors**. It covers cloning IR0, checking the
host, building the supported desktop image, reading man pages, inspecting an
optional boot log on the host, and landing a first small change.

Out of scope: rewriting the kernel, ARM appliance flash, or claiming full
Linux ABI. Subsystem depth lives in other `IR0-*` pages.

## 2. Internal architecture (entry surface)

| Piece | Role |
|-------|------|
| `make check-env` / `deptest` | Host diagnostic (required / optional / unusable) |
| `make defconfig` / `ir0` / `run` | Official first boot (x86-64 QEMU) |
| `make help-profiles` | Product profiles + honest ceilings |
| `make sync-mandocs` / `make man TOPIC=` | Docs without MANPATH fights |
| `make pre-submit` | Local contributor gate → `PRE_SUBMIT_OK` |
| `CONFIG_BOOT_LOG_HOSTSHARE` | Opt-in dump of log ring → virtio-9p `ir0-boot.log` |

## 3. Data flow — first boot

```text
  git clone → cd IR0
       → make check-env
       → make defconfig
       → make ir0          # kernel-x64.iso
       → make run          # QEMU (Supported path)
```

Optional boot log on the **host** (not required for first run):

```text
  make run-bootlog
       → enables BOOT_LOG_HOSTSHARE
       → QEMU -virtfs mount_tag=ir0share
       → guest writes build/hostshare/ir0-boot.log
       → serial tag BOOT_LOG_HOSTSHARE_OK
```

## 4. Responsibilities

- Contributor: run `check-env` before reporting “build broken”.
- Maintainer paths: keep Makefile root thin; recipes in `scripts/make/*.mk`.
- Kernel: never panic if 9p is absent; SKIP tag when opt-in but no device.

## 5. Subsystem boundaries

- Do not add agent one-shot smokes to the root Makefile.
- Do not claim Raspberry Pi images are SD-flashable (lab UART / stub only).
- Mandocs document **implemented** behavior; aspirational items only in §10.

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|-------------|
| IR0-boot | Boot pipeline / serial banner contract |
| IR0-multi-arch | ARM lab ceilings |
| filesystems / virtio-9p | Host share used by boot-log dump |
| CONTRIBUTING.md | Style, commit, pre-submit |

## 7. Visual maps

```text
  New contributor
       │
       ├─► check-env ──► defconfig ──► ir0 ──► run
       │                                      │
       │                                      └─► (optional) run-bootlog
       │                                                └─► host ir0-boot.log
       │
       └─► sync-mandocs ──► man TOPIC=onboarding|boot|mm|…
                              └─► first bug ──► pre-submit
```

## 8. Important invariants

1. Official first boot = **desktop x86-64 under QEMU** only.
2. `hub-rpi4` = Hardware lab / UART; `watch-rpi5-stub` = Planned compile stub.
3. Boot log hostshare is **opt-in** (`BOOT_LOG_HOSTSHARE`); `make run` stays clean.
4. Facade headers under `includes/ir0/` are **not** all covered by mandocs — see INDEX.

## 9. Debugging tips — first bug walkthrough

1. Boot with `make run` (or `run-console`) and note the serial banner
   `IR0 Kernel v… Boot routine`.
2. `make man TOPIC=boot` — find `ir0_boot_serial_ready` / `includes/ir0/boot_log.h`.
3. Grep the tree for that symbol; open the C file; read one call site in `kmain`.
4. Optional: `make run-bootlog` and open `build/hostshare/ir0-boot.log` on the host
   (Linux-dmesg *idea*: consultable buffer — not a 2000-line ACPI dump).
5. Make a **one-file** doc or comment fix, then:

```bash
make pre-submit
# expect PRE_SUBMIT_OK
```

## 10. Future roadmap

- More “first bug” recipes per subsystem (mm, vfs, net).
- Broader mandoc coverage of `includes/ir0/` (honest backlog — not done).
- In-guest `man` on userspace ISO (depends on userspace maturity).

See also: `README.md`, `SETUP.md`, `CONTRIBUTING.md`, `make help-bootlog`.
