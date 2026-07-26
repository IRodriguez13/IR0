# IR0 Security and Credentials

| Field | Value |
|-------|-------|
| Version | 0.2 |
| IR0 phase | T0–T1 |
| Status | stable |
| Depends on | process, vfs, syscalls |
| Man page | IR0-security (section 7) |
| Primary sources | `kernel/credentials.c`, `kernel/elf_loader.c`, `kernel/process.h`, `fs/permissions.c`, `fs/vfs.c`, `includes/ir0/credentials.h` |

> **Last verified:** 2026-07-25

## 1. Overview

IR0 uses a Unix credential model: per-process real/effective/saved
`uid`/`gid`, supplementary groups, `umask`, and path checks via effective IDs.
There is no Linux capability set.

Privilege elevation is **userspace policy**, not a kernel service: the kernel
enforces set-user-ID/set-group-ID bits on `execve`, and the product userspace
(`IR0-userspace`) ships **OpenDoas** with `permit persist :wheel as root`. The
old IR0-specific `sudo_auth` syscall (404) and its hardcoded passwords were
removed; the number is retired in `includes/uapi/ir0/syscall_linux.h`.

## 2. Internal architecture

| Piece | Role |
|-------|------|
| `credentials.c` | `ir0_current_cred`, `ir0_check_file_access` |
| `permissions.c` | `ir0_access_from_stat` |
| `process_t` | uid, gid, euid, egid, `suid`, `sgid`, `groups[]`, `ngroups`, `no_new_privs`, umask |
| `elf_loader.c` | set-id bits on exec, `AT_SECURE` in auxv |
| VFS | traverse checks, chmod/chown/mount policy |
| Syscalls | get/set{res}uid/gid, setgroups, umask, chmod, chown, access, `PR_SET_NO_NEW_PRIVS` |

**Account database:** real `/etc/passwd`, `/etc/shadow` (mode 0600) and
`/etc/group`, produced by the userspace tree. Hashes are SHA-512 `crypt(3)`;
there are no passwords in kernel source.

## 3. Data flow

**File access check:**

```text
  open/stat/access path
       → ir0_stat_path_routed (proc/sys/dev/vfs)
       → ir0_access_from_stat(st, mode, euid, egid)
       → euid==0 allow all
       → else owner/group/other rwx bits on st_mode
```

**Elevation (set-id exec):**

```text
  execve(/usr/bin/doas)          ← mode 4755, owner root
       → elf_loader reads st_mode
       → no_new_privs set?  → ignore set-id bits
       → S_ISUID → euid = st_uid (suid keeps the real id for setresuid)
       → AT_SECURE = 1 in auxv
       → doas authenticates the caller against /etc/shadow and checks :wheel
```

ASCII:

```text
  syscall ──► ir0_current_cred() ──► euid/egid
                      │
                      ▼
              ir0_check_file_access
                      │
                      ▼
              VFS backend (minix/tmpfs enforce too)
```

## 4. Responsibilities

- VFS: `check_dir_traverse` requires execute on each path component (non-root).
- chmod: root or file owner at syscall + VFS boundary.
- chown: root only at VFS; minix backend also root-only.
- mount/umount: root cred required.

## 5. Subsystem boundaries

- Authentication and elevation policy live in userspace (`IR0-userspace`), not
  in the kernel: the kernel only provides identity syscalls, set-id exec,
  `/proc/[pid]/stat` and file permission checks.
- No PAM and no capabilities.

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|---------------|
| VFS | all path operations |
| Process | cred inheritance on spawn/fork |
| Syscalls | identity and permission syscalls |
| IR0-userspace | login/getty, `passwd`, OpenDoas; `smoke-setuid-exec`, `smoke-passwd`, `smoke-doas` |

## 7. Visual maps

```text
  subject (euid,egid) ──► object (st_uid,st_gid,mode)
              │
              ├─ root → allow
              └─ else → rwx triple match
```

## 8. Important invariants

1. `ROOT_UID`/`ROOT_GID` = 0; default spawn without parent uses root + umask 0022.
2. `ir0_current_cred()` with no process returns boot root stub.
3. No `CAP_*` checks anywhere.
4. `no_new_privs` is inherited on fork and disables set-id bits on exec.
5. New files take the **effective** uid/gid of the creator (POSIX), which is
   what makes the doas persist timestamp file pass its ownership check.
6. `vfs_utimens` dispatches to the backend; MINIX stores `i_time` in seconds.
7. MINIX v1 directory entries cap names at 14 bytes — sentinels such as
   `/etc/ir0-noroot` must stay within that limit or they are silently truncated.
8. ACLs not implemented.

## 9. Debugging tips

- `whoami`, `id` debug commands show effective ids.
- `-EACCES` on traverse: missing execute on directory component.
- `-EPERM` on mount/chown: need root euid.

## 10. Future roadmap

- Capability set.
- ACLs on tmpfs/minix (noted in IR0-vfs §10).
- Signal send permissions.
- Namespaces — not planned.

See: `IR0-vfs`, `IR0-process`, `IR0-syscalls`.
