# IR0 Kernel Manager (kmang)

| Field | Value |
|-------|-------|
| Version | 0.1 |
| IR0 phase | T1 (host tooling for persistent ISD machines) |
| Status | stable |
| Depends on | userspace, boot, onboarding |
| Man page | IR0-kmang (section 7) |
| Primary sources | `scripts/kernel_manager.py`, `scripts/make/isd.mk`, `scripts/test_kernel_manager.py`, `scripts/test-isd-contracts.sh` |

> **Last verified:** 2026-09-20

## 1. Overview

**kmang** (`make kmang`) is the interactive TUI and CLI for a **persistent-machine
kernel ISO catalog**. Each machine under `IR0-machines/<arch>/<profile>/<name>/`
keeps versioned boot ISOs, a **Default** symlink (`kernel-current.iso`), and an
optional **Fallback** (`kernel-fallback.iso`).

`make poweron` boots the catalog **Default** when `resolve` succeeds — not the bare
workspace ISO until you enroll it (`i` in the TUI, or `make kernel-manager-install`).

Build numbers embedded as `#N` / `-buildN` are **machine-local** counters
(`.build_number` in the kernel tree). They are not global release IDs. Compare hosts
by SHA-256, ELF `ir0_build_number`, and install-time git metadata — not by `#N` alone.

**Out of scope:** userspace/rootfs versioning (`make usmang`), in-guest KTM tests
(`Documentation/KTM.md`), and one-shot `make run-isd` (always workspace ISO).

## 2. Internal architecture

| Component | Path | Role |
|-----------|------|------|
| Store | `KernelStore` in `scripts/kernel_manager.py` | Catalog CRUD, verify, resolve |
| Installed images | `<machine-dir>/kernels/<id>.iso` | Immutable enrolled ISO copies |
| Metadata | `<machine-dir>/kernels/<id>.json` | format 3: sha256, embedded_build, arch, git, provenance |
| Default link | `<machine-dir>/kernel-current.iso` | Symlink → selected ISO |
| Fallback link | `<machine-dir>/kernel-fallback.iso` | Previous Default after re-select |
| Lock | `<machine-dir>/.kernel-manager.lock` | Exclusive flock — one manager per machine |
| Workspace source | `kernel-x64-userspace.iso` | Built ISO; not booted by `poweron` until enrolled |

**Catalog id:** `{IR0_VERSION_STRING}-build{N}` where `N` comes from `nm ir0_build_number`
inside the ISO kernel ELF — never from the filename alone.

**Verification layers:**

1. ISO contains extractable `/boot/kernel-x64.bin`, `/boot/kernel-arm64.bin`, or `/boot/kernel.bin`.
2. ELF `e_machine` matches catalog `--arch` when configured.
3. Embedded build matches `-buildN` suffix when present.
4. On-disk SHA-256 and metadata format/id/arch consistency.

Health states: `verified`, `checksum-only`, `legacy-unverified`, or failure reasons
(`checksum mismatch`, `identity mismatch`, …).

## 3. Data flow

**Enroll workspace ISO (TUI `i` or `install-workspace`):**

```text
  make kernel-x64-userspace.iso
       → source_identity(version, embedded #N)
       → copy to kernels/<version-buildN>.iso (+ fsync)
       → write .json metadata (git commit, builder@host, sha256)
       → atomic symlink kernel-current.iso → enrolled ISO
       → previous Default → kernel-fallback.iso
```

**Select + boot (`Enter` or `b`) on desktop profiles:**

```text
  Enter on kernel → select as Default (+ Fallback rollover)
       → prompt: t = terminal only | x = X direct | q = stay in kmang
       → inject etc/ir0-session on machine disk (one-shot)
       → exit TUI → make poweron

  b on Default → resolve() → same prompt → poweron
```

Guest `/etc/profile` (ISD) reads the one-shot file on console login, then deletes it.
`x` forces `startx`; `terminal` skips auto-X even on `PROFILE=desktop` disks.

**Boot persistent machine (`make poweron`):**

```text
  kernel_manager resolve
       → verify Default; on failure try Fallback
       → on total failure: workspace ISO if catalog empty
       → on enrolled-but-all-corrupt: exit 2 (does not silently boot workspace)
       → qemu -cdrom <resolved ISO> -drive machine disk.img
```

**Rebuild without enroll (`make machine-update-kernel`):**

```text
  rebuild workspace ISO only
       → poweron still uses old Default until kmang i / kernel-manager-install
```

## 4. Responsibilities

- kmang **must** reject identifier/path traversal, build/id mismatches, arch/ELF
  mismatches, and same-id ISO collisions with different bytes.
- kmang **must** keep Default/Fallback bootable via `resolve()` or fail loudly.
- kmang **must** label `#N` as machine-local in TUI help and `compare` JSON.
- Operators **must** run `i` or `kernel-manager-install` after rebuilding the kernel
  if they want `poweron` to pick up the new image.
- `machine-update-kernel` **must not** auto-enroll (disk preservation contract).

## 5. Subsystem boundaries

- kmang does **not** repack the full MINIX rootfs — only optional
  `etc/ir0-session` before a boot you requested from the TUI.
- kmang does **not** replace ISD package/version management (`userspace_manager.py`).
- kmang does **not** run QEMU; `poweron` consumes `resolve` output only.
- Host tests live in `scripts/test_kernel_manager.py`; gate via `make isd-contracts`.

## 6. Relations to other subsystems

| Subsystem | Relation |
|-----------|----------|
| `scripts/make/isd.mk` | Defines `KMANG_PY`, `make kmang`, `kernel-manager-*`, `poweron` |
| `IR0-userspace` / ISD | Rootfs disk separate from kernel catalog |
| `make usmang` | Complementary host inspector for ISD userland |
| Boot / version stamp | `IR0_VERSION_STRING` prefixes catalog ids |
| KTM | Independent in-guest test plane — not enrolled by kmang |

See also: `IR0-userspace`, `IR0-onboarding`, `Documentation/TOOLING.md`.

## 7. Visual maps

```text
  Workspace tree                    Persistent machine store
  ----------------                  -------------------------
  kernel-x64-userspace.iso          kernels/
  .build_number (local N)    i/r     ├── 0.0.1-pre-rc3-build42.iso
                                    ├── 0.0.1-pre-rc3-build42.json
                                    kernel-current.iso ──► build42.iso  (Default)
                                    kernel-fallback.iso ─► build41.iso  (Fallback)
                                    disk.img              (+ one-shot session file on boot)

  make poweron ──► resolve() ──► qemu -cdrom Default ISO
```

## 8. Important invariants

| Invariant | Why |
|-----------|-----|
| Default ≠ Workspace until enroll | Prevents silent boot of unreviewed local builds |
| Fallback survives select | Roll back with Enter on previous entry after verify |
| `ir0_build_number` is SoT for `#N` | Filename/id suffix must match ELF symbol |
| SHA-256 is SoT across hosts | Two machines can both be `build42` with different bytes |
| Current/Fallback cannot be `delete`d | Avoid bricking `resolve` with empty catalog |
| Login session one-shot | `/etc/ir0-session` consumed on first console login |
| Single writer lock | Parallel TUI/CLI on same machine returns exit 2 |

## 9. Debugging tips

| Symptom | Check |
|---------|-------|
| `poweron` boots old kernel | `make kmang` → compare line; press `i` or `make kernel-manager-install` |
| `kmang has installed kernels but none verifies` | `KMANG_PY verify`; re-enroll or restore Fallback |
| `identity mismatch` / `collision` | Workspace rebuild changed bytes; use new `-buildN` or delete stale entry |
| `another kernel manager is active` | Close other TUI; remove stale lock only if no process holds it |
| Wrong arch enrolled | `--arch` on CLI must match ELF; x86_64 ISO cannot install on arm64 catalog |
| Forgot a TUI key | Press **`h`**, **`?`**, or **F1** inside `make kmang`; compact legend on bottom row |

**TUI key legend (full):** grouped overlay via `h` / `?` / F1; scroll with ↑↓ when the
terminal is short. Non-interactive copy: `python3 scripts/kernel_manager.py … help` or
`make kmang-cli` plus `help` subcommand on `KMANG_PY`.

**CLI (non-interactive):**

```bash
make kernel-manager-list          # list + workspace + compare
make kmang-cli                    # list --json
make kernel-manager-install       # rebuild + install-workspace
python3 scripts/kernel_manager.py --machine-dir … compare
python3 scripts/kernel_manager.py --machine-dir … info ID --json
python3 scripts/kernel_manager.py --machine-dir … help
```

**Tests:** `python3 scripts/test_kernel_manager.py` (also `make kmang-test`, `make isd-contracts`).

## 10. Future roadmap

- First-class arm64 catalog profiles when ISD persistent arm64 machines ship
  (verify already extracts arm64 payloads; primary product path is still x86_64).
- Optional auto-prompt after `machine-update-kernel` (today intentionally manual).
- `IR0-system` manifest pinning Default kernel SHA + ISD version in one host view.

## Build

```bash
make sync-mandocs
make man TOPIC=kmang
```
