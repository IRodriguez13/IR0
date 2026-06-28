# IR0 — Capability board (release certification)

> **Last verified:** 2026-06-26  
> **Source of truth:** `make linux-abi-audit`, `make smoke-release-0.0.1`,  
> `scripts/linux_abi/contracts.json`, `Documentation/ai_driven_dev/linux_ground_truth.md`,  
> `kernel/test/`, `tests/host/`

## Strategy shift

Development axis moves from **individual syscall contracts** to **certified system
capabilities**. Users do not invoke isolated syscalls; they rely on complete behaviours
(create files, run processes, use a terminal, mount storage, etc.).

Contract-level detail (transitional): `Documentation/releases/IR0_0.0.1_ABI_BOARD.md`.

---

## Status legend

| Status | Meaning |
|--------|---------|
| **VERIFIED** | Full capability certified: Linux ground truth + paired workload + audit PASS for every syscall in scope |
| **LINUX-LIKE** | Subset green (ktest/smoke/host); audit incomplete or first divergence not closed |
| **BLOCKED** | First Linux↔IR0 divergence documented; no new features until unblocked |
| **TODO** | Not started or no paired workload |

A capability is **VERIFIED** only when **all** listed syscalls/behaviours in that row pass
the audit bundle — not when a single contract passes in isolation.

---

## Linux-first workflow (mandatory)

For each capability iteration:

1. Linux ground truth (`strace`, `/proc/self/maps`, man pages, musl behaviour).
2. Linux workload (same ELF or equivalent probe).
3. IR0 workload (QEMU + injected init or tier smoke).
4. First observable divergence (order, errno, return, VMA, signal).
5. Minimal fix in IR0 (no invented semantics).
6. Audit PASS (`make linux-abi-audit*` or capability-specific gate).
7. Commit.

No implementation without step 1–4 evidence.

---

## 0.0.1 priority order

1. **Filesystem RW** — close (VFS + MINIX backend)
2. **Process lifecycle**
3. **Memory ABI**
4. **IPC**
5. **Terminal**
6. **ELF runtime** (loader + auxv as part of process bring-up)
7. Networking — deferred
8. Graphics / X11 — deferred

**Release rule:** no new features while a **0.0.1-priority** capability remains **BLOCKED**.
Goal of 0.0.1: a **small set of fully certified capabilities**, not syscall count.

---

## Capability summary

| Capability | Status | Primary gate | Blocks 0.0.1? |
|------------|--------|--------------|---------------|
| Filesystem RW (VFS) | **VERIFIED** | `linux-abi-audit-vfs-write`, openat, stat | No |
| Process lifecycle | **LINUX-LIKE** | ktests; `wait4` audit only | Yes (partial) |
| Memory ABI | **LINUX-LIKE** | `linux-abi-audit` brk/mmap; `smoke-mm-cow-lazy` | Yes (partial) |
| IPC | **LINUX-LIKE** | ktests/host subsets | Yes |
| Terminal | **LINUX-LIKE** | `smoke-runit-ash-interactive` | Yes (partial) |
| Filesystems (backends) | mixed | see below | FAT16 rw yes |
| Networking | **TODO** | — | No (0.0.2) |
| Graphics | **TODO** | — | No (0.0.2) |

---

## Filesystem RW

**Status:** **VERIFIED**

Certified on **VFS + MINIX/tmpfs** path (not FAT16 write). All syscalls below share
bundle `vfs_write` (29 steps, `/tmp/ir0wtest`) plus dedicated openat/stat audits.

| Syscall / behaviour | Status | Evidence |
|----------------------|--------|----------|
| open / openat | VERIFIED | `linux-abi-audit-openat`, vfs_write O_CREAT/TRUNC/APPEND/EXCL |
| close | VERIFIED | openat audit EBADF |
| read | VERIFIED | `linux-abi-audit-read`, vfs_write roundtrip |
| write | VERIFIED | vfs_write bundle |
| stat / fstat | VERIFIED | `linux-abi-audit-stat` |
| lseek | VERIFIED | vfs_write SEEK_SET/CUR/END |
| truncate / ftruncate | VERIFIED | vfs_write; MINIX grow + `sys_ftruncate` |
| rename | VERIFIED | vfs_write |
| unlink | VERIFIED | vfs_write |
| mkdir / rmdir | VERIFIED | vfs_write |

**Impact for userspace:** BusyBox file utilities on MINIX root (`cp`, `mv`, `rm`,
`mkdir`, `touch`, shell redirection) with predictable errno.

**Unlocks:** tmpfs/minix workspace, manifest probes, tier-1 rootfs mutation without
special-case syscalls.

**Next within FS:** certify **same bundle on FAT16** before enabling FAT16 RW (see
backends).

---

## Process lifecycle

**Status:** **LINUX-LIKE**

| Syscall / behaviour | Status | Evidence |
|----------------------|--------|----------|
| fork | LINUX-LIKE | ktests partial; no capability audit |
| execve | LINUX-LIKE | tier-1 smokes; audit disabled in `contracts.json` |
| wait4 | VERIFIED | `linux-abi-audit-wait4` (single syscall only) |
| exit | LINUX-LIKE | ktests partial |
| signals (minimal) | LINUX-LIKE | host `test_signal_rt_sigaction_abi` |
| argv / envp / auxv | LINUX-LIKE | ELF loader smokes; no paired audit |

**Impact for userspace:** `/sbin/init`, runit, basic shell; **not** full job control or
robust signal semantics for all musl paths.

**Unlocks when VERIFIED:** static BusyBox applets, `su`/`sudo` paths, TCC in-guest link
(musl expects coherent fork/exec/wait).

**Next capability work:** single **process lifecycle audit bundle** (fork → execve →
wait4 → exit + minimal SIGCHLD), not isolated `execve` contract.

---

## Memory ABI

**Status:** **LINUX-LIKE**

| Syscall / behaviour | Status | Evidence |
|----------------------|--------|----------|
| brk | VERIFIED | `linux-abi-audit` |
| mmap | VERIFIED | `linux-abi-audit-mmap` (anon RW, PROT_NONE, MAP_FIXED, bad fd) |
| munmap | LINUX-LIKE | covered in mmap audit steps; standalone contract disabled |
| mprotect | TODO | no audit |
| lazy allocation | LINUX-LIKE | `CONFIG_LAZY_*`, `smoke-mm-cow-lazy` |
| COW | LINUX-LIKE | FASE40 smoke + ktests |
| stack | LINUX-LIKE | gap policy in `mmap_contract.h`; not Linux ASLR |
| heap | LINUX-LIKE | brk delta OK; absolute VA may differ |

**Impact for userspace:** musl malloc/brk, anon mmap; **no** file-backed mmap parity.

**Unlocks when VERIFIED:** musl-heavy static binaries, larger heap growth without surprises.

**Next:** capability audit for **brk + mmap + munmap + mprotect** as one Memory ABI gate.

---

## IPC

**Status:** **LINUX-LIKE**

| Syscall / behaviour | Status | Evidence |
|----------------------|--------|----------|
| pipe | LINUX-LIKE | ktest `syscall_pipe`, host tests |
| dup / dup2 | LINUX-LIKE | ktests partial |
| poll | LINUX-LIKE | host KTM matrix |
| select | TODO | no audit |
| fcntl (minimal) | LINUX-LIKE | FD_CLOEXEC paths; partial |

**Impact for userspace:** shell pipes rudimentary; **not** reliable poll-driven daemons.

**Unlocks when VERIFIED:** shell pipelines, simple client/server over pipe, musl
`poll`-based I/O multiplexing.

**Next:** **IPC capability bundle** (pipe + dup2 + poll + EAGAIN/EINTR ordering).

---

## Terminal

**Status:** **LINUX-LIKE**

| Syscall / behaviour | Status | Evidence |
|----------------------|--------|----------|
| TTY read/write | LINUX-LIKE | `/dev/console`, ktests `tty_canon_*` |
| canonical mode | LINUX-LIKE | ash smoke `echo hi` tags |
| read wake | LINUX-LIKE | D1.13–D1.16 TTY path; smoke harness hardened (D1.21) |
| termios | LINUX-LIKE | partial; not full Linux termios audit |
| PTY | TODO | 0.0.2 scope |

**Impact for userspace:** runit + BusyBox ash on `/dev/console`; **not** SSH, script(1),
or full ncurses on PTY.

**Unlocks when VERIFIED:** interactive shell as default session, getty-class bring-up.

**Next:** **Terminal capability audit** (canonical read + termios subset + wake), then
PTY in 0.0.2.

---

## Filesystems (backends)

Do **not** mix VFS certification with backend-specific behaviour.

### VFS contract (path ops semantics)

**Status:** **VERIFIED** — see Filesystem RW; backend-agnostic errno and create/truncate
rules (`Documentation/releases/IR0_0.0.1_VFS_WRITE_PLAN.md`).

### MINIX backend

**Status:** **VERIFIED** (rw path) — vfs_write + mount on root; truncate grow fixed R3.

### FAT16 backend

**Status:** **LINUX-LIKE** (read-only)

| Item | Status | Gate |
|------|--------|------|
| Mount + read | VERIFIED | `smoke-fat16-mount` |
| Write/create/truncate | **BLOCKED** | requires `linux-abi-audit-vfs-write` on FAT16 disk |

### EXT2 backend

**Status:** **TODO**

### IR0FS / simplefs backend

**Status:** **LINUX-LIKE** — debug/tier0; not release-certified.

---

## Networking

**Status:** **TODO** (0.0.2+)

Scope (future): sockets, loopback, TCP, UDP, DNS.

---

## Graphics

**Status:** **TODO** (0.0.2+)

Scope (future): framebuffer, evdev/input, mmap fb, X11 bootstrap.

---

## Release 0.0.1 vs 0.0.2

### 0.0.1 (current target)

Certified or closing:

- Filesystem RW on VFS/MINIX ✓
- Release gate green: `make release-0.0.1` (phase1 + full `linux-abi-audit` + ash + FAT16
  read smoke)
- Remaining **LINUX-LIKE** capabilities must not regress to **BLOCKED**; new features
  wait until Process lifecycle or next priority capability is audit-closed.

### 0.0.2 (after base capabilities VERIFIED)

- PTY + full termios
- poll/select complete
- sockets / loopback
- FAT16 RW (post vfs_write on FAT16)
- EXT2
- ELF loader hardening
- X11 bootstrap

---

## Gates reference

```bash
make linux-abi-audit              # all enabled contracts (8 today)
make linux-abi-audit-vfs-write    # Filesystem RW bundle
make smoke-release-0.0.1          # phase1 + audit + ash + FAT16 read
make release-0.0.1                # + kernel-text-budget
```

Registry: `scripts/linux_abi/contracts.json` — enable bundles by **capability**, not
syscall trivia.

---

## Report footer (mandatory from 2026-06-26)

Every oleada / milestone report ends with:

```
Capability: <name>
Estado: VERIFIED | LINUX-LIKE | BLOCKED | TODO

Motivo del estado: <first divergence or gate reference>

Impacto para userspace: <what works / what fails predictably>

Qué software real desbloquea: <BusyBox/musl/runit/…>

Qué capability es la siguiente: <one item from priority list>
```

---

## Related documents

| Document | Role |
|----------|------|
| `Documentation/releases/IR0_0.0.1_ABI_BOARD.md` | Contract-level view (transitional) |
| `Documentation/ai_driven_dev/linux_ground_truth.md` | Per-syscall evidence table |
| `Documentation/releases/IR0_0.0.1_SCOPE.md` | Release gate inventory |
| `Documentation/releases/IR0_0.0.1_VFS_WRITE_PLAN.md` | R3 vfs_write bundle plan |
