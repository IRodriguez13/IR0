# IR0 — Capability board (release certification)

> **Last verified:** 2026-09-22  
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
| Process lifecycle | **VERIFIED** | `linux-abi-audit-process-lifecycle`, wait4 | No |
| Memory ABI | **VERIFIED** | `linux-abi-audit-memory-bundle` (brk/mmap/munmap/mprotect) | No |
| IPC | **VERIFIED** | `linux-abi-audit-ipc-bundle` (pipe/poll/dup/munmap) | No |
| Terminal | **VERIFIED** | `linux-abi-audit-ioctl`, PTY multiplex, session smokes | No |
| Filesystems (backends) | mixed | see below | FAT16 rw verified on audit |
| Networking | **TODO** | — | No (0.0.2) |
| Graphics / fb+evdev | **LINUX-LIKE** | `smoke-desk-xfbdev` (mini harness); TinyX **BLOCKED** | No (0.0.2) |

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

**Status:** **VERIFIED**

| Syscall / behaviour | Status | Evidence |
|----------------------|--------|----------|
| fork | VERIFIED | `linux-abi-audit-process-lifecycle` bundle |
| execve | VERIFIED | `linux-abi-audit-execve` + lifecycle bundle |
| wait4 | VERIFIED | `linux-abi-audit-wait4`, lifecycle bundle |
| exit | VERIFIED | lifecycle bundle ktest evidence |
| kill (SIGTERM) | VERIFIED | `linux-abi-audit-kill-sigterm` + ktest `kill_sigterm_wait_status` |
| signals (minimal) | LINUX-LIKE | host `test_signal_rt_sigaction_abi`; sigreturn contract VERIFIED |
| argv / envp / auxv | LINUX-LIKE | ELF loader smokes; no dedicated audit |

**Impact for userspace:** `/sbin/init`, runit, BusyBox ash, `su`/`doas` session paths;
job control and full musl signal paths still partial.

**Next within process:** optional `fork`/`exit` standalone contracts; SIGCHLD edge cases
via session smokes (`smoke-session-chaos`).

---

## Memory ABI

**Status:** **VERIFIED**

| Syscall / behaviour | Status | Evidence |
|----------------------|--------|----------|
| brk | VERIFIED | `linux-abi-audit` |
| mmap | VERIFIED | `linux-abi-audit-mmap` (anon RW, PROT_NONE, MAP_FIXED, bad fd) |
| munmap | VERIFIED | `linux-abi-audit-munmap` / memory bundle |
| mprotect | VERIFIED | `linux-abi-audit-mprotect` (RO/RW toggle; unaligned → EINVAL) |
| lazy allocation | LINUX-LIKE | `CONFIG_LAZY_*`, `smoke-mm-cow-lazy` |
| COW | LINUX-LIKE | FASE40 smoke + ktests |
| stack | LINUX-LIKE | gap policy in `mmap_contract.h`; not Linux ASLR |
| heap | LINUX-LIKE | brk delta OK; absolute VA may differ |

**Impact for userspace:** musl malloc/brk, anon mmap, RELRO-class mprotect on exec
mappings; **no** file-backed mmap parity.

**Gate:** `make linux-abi-audit-memory-bundle` (brk via full audit; mmap/munmap/mprotect).

---

## IPC

**Status:** **VERIFIED**

| Syscall / behaviour | Status | Evidence |
|----------------------|--------|----------|
| pipe | VERIFIED | `linux-abi-audit-pipe` |
| dup / dup2 | VERIFIED | `linux-abi-audit-dup` |
| poll | VERIFIED | `linux-abi-audit-poll` |
| select | TODO | no audit |
| fcntl | VERIFIED | `linux-abi-audit-fcntl`: F_GETFD/F_SETFD/F_GETFL, F_DUPFD, F_DUPFD_CLOEXEC=1030 |

**Impact for userspace:** shell pipelines, poll-driven I/O, CLOEXEC dup via fcntl.
`select` still unaudited. Remaining fcntl cmds (locks, F_SETFL) are out of this
contract.

**Next within IPC:** `select` audit or honest ENOSYS policy; pipe `EINTR` ordering smokes.

---

## Terminal

**Status:** **VERIFIED**

| Syscall / behaviour | Status | Evidence |
|----------------------|--------|----------|
| TTY read/write | VERIFIED | `/dev/console`, ktests `tty_canon_*`, ash smoke |
| canonical mode | VERIFIED | ash smoke `echo hi` tags |
| read wake | LINUX-LIKE | D1.13–D1.16 TTY path; `tty_canon_block_wake` ktest (acceptable-diff vs Linux block semantics) |
| termios / ioctl | VERIFIED | `linux-abi-audit-ioctl` (TCGETS, TIOCGWINSZ, TIOCGPGRP) |
| PTY | VERIFIED | `linux-abi-audit-pty-multiplex`, `smoke-ext2-startx PROFILE=desktop` |

**Impact for userspace:** runit + BusyBox ash on `/dev/console`; dual PTY xterm desktop
smoke; **not** SSH, script(1), or full ncurses termios surface.

**0.0.2 debt:** full termios ioctls, `select`, canonical block wake parity audit.

---

## Filesystems (backends)

Do **not** mix VFS certification with backend-specific behaviour.

### VFS contract (path ops semantics)

**Status:** **VERIFIED** — see Filesystem RW; backend-agnostic errno and create/truncate
rules (`Documentation/releases/IR0_0.0.1_VFS_WRITE_PLAN.md`).

### MINIX backend

**Status:** **VERIFIED** (rw path) — vfs_write + mount on root; truncate grow fixed R3.

### FAT16 backend

**Status:** **VERIFIED** (rw path on audit disk)

| Item | Status | Gate |
|------|--------|------|
| Mount + read | VERIFIED | `smoke-fat16-mount` |
| Write/create/truncate | **VERIFIED** | `linux-abi-audit-vfs-write-fat` |

### EXT2 backend

**Status:** **LINUX-LIKE** — STO-2/3 lab path (root PID1 boot, dual-run matrix). Not yet
capability-audited on a dedicated ext2 block workload (`linux-abi-audit-vfs-write` on ext2
image remains P1). Single block-group / linear dir lookup limits apply.

### IR0FS / simplefs backend

**Status:** **LINUX-LIKE** — debug/tier0; not release-certified.

---

## Networking

**Status:** **TODO** (0.0.2+)

Scope (future): sockets, loopback, TCP, UDP, DNS.

---

## Graphics / tier-2 prep

**Status:** **LINUX-LIKE** (fb+evdev prep only); **TinyX BLOCKED**

| Item | Status | Gate |
|------|--------|------|
| `/dev/fb0` + `/dev/events0` | LINUX-LIKE | `make smoke-desk-xfbdev` (`XSERVER_SELECT mini`) |
| Out-of-tree desk harness | LINUX-LIKE | `../IR0-desktop`, `make desktop-maintainer-check` |
| TinyX / Xfbdev guest | **BLOCKED** | `force_tinyx` lab — `#PF`/`CONTEXT_LIFETIME_BROKEN` (see TINYX_LAB.md) |
| Full X11 / WM product | **TODO** | **0.0.2+** — explicitly **out of 0.0.1 release scope** |

**0.0.1 rule:** tier-2 graphics prep (fb mmap, evdev inject) may smoke green; **no**
0.0.1 certification claim for TinyX, GTK, or desktop ISO ship.

---

## Release 0.0.1 vs 0.0.2

### 0.0.1 (current target)

Certified or closing:

- Filesystem RW on VFS/MINIX ✓
- Process lifecycle ✓
- Memory ABI ✓ (`linux-abi-audit-memory-bundle`)
- IPC ✓
- Terminal ✓ (`linux-abi-audit-ioctl` + PTY)
- Release gate green: `make release-0.0.1` (phase1 + full `linux-abi-audit` + ash + FAT16
  read smoke)
- **TinyX / full X11:** explicitly **deferred** to 0.0.2 (fb+evdev prep only in 0.0.1)

### 0.0.2 (after base capabilities VERIFIED)

- TinyX guest stable + desk session product path
- Full termios / select surface
- sockets / loopback
- EXT2
- ELF loader hardening
- Desktop rootfs ISO ship

---

## Gates reference

```bash
make linux-abi-audit              # all enabled contracts
make linux-abi-audit-vfs-write    # Filesystem RW bundle (MINIX/tmpfs)
make linux-abi-audit-vfs-write-fat
make linux-abi-audit-memory-bundle # brk + mmap + munmap + mprotect
make linux-abi-audit-ioctl         # console termios subset
make release-0.0.1-capabilities   # memory + IPC + process + ioctl + terminal smokes
make smoke-release-0.0.1          # phase1 + audit + ash + FAT16 read
make release-0.0.1                # + kernel-text-budget
make desktop-maintainer-check     # optional; requires ../IR0-desktop
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
