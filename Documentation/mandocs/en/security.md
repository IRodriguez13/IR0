# IR0 Security and Credentials

| Field | Value |
|-------|-------|
| Version | 0.1 |
| IR0 phase | T0–T1 |
| Status | stable |
| Depends on | process, vfs, syscalls |
| Man page | IR0-security (section 7) |
| Primary sources | `kernel/credentials.c`, `fs/permissions.c`, `fs/vfs.c`, `kernel/syscalls.c`, `includes/ir0/permissions.h` |

## 1. Overview

IR0 uses a minimal Unix-like credential model: per-process `uid/gid/euid/egid`,
`umask`, and path checks via effective IDs. There is no Linux capability set.
Elevation for debug workflows uses the IR0-specific **`sudo_auth`** syscall with
hardcoded passwords in `fs/permissions.c`.

## 2. Internal architecture

| Piece | Role |
|-------|------|
| `credentials.c` | `ir0_current_cred`, `ir0_check_file_access` |
| `permissions.c` | `ir0_access_from_stat`, `auth_user_password` |
| `process_t` | uid, gid, euid, egid, umask |
| VFS | traverse checks, chmod/chown/mount policy |
| Syscalls | get/set uid/gid, umask, chmod, chown, access, sudo_auth |

**Hardcoded users (`permissions.c`):**

```text
  root / uid 0 / password "root"
  user / uid 1000 / password "ir0"
```

## 3. Data flow

**File access check:**

```text
  open/stat/access path
       → ir0_stat_path_routed (proc/sys/dev/vfs)
       → ir0_access_from_stat(st, mode, euid, egid)
       → euid==0 allow all
       → else owner/group/other rwx bits on st_mode
```

**sudo_auth:**

```text
  sys_sudo_auth(password)
       → if already root: success
       → auth_user_password → on match set euid=0, egid=0 (permanent for process)
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

- No supplementary groups; effective uid/gid only.
- No setuid/setgid **bit** enforcement on exec.
- Passwords plaintext in source — not production security model.
- No `/etc/shadow`, PAM, or capabilities.

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|---------------|
| VFS | all path operations |
| Process | cred inheritance on spawn/fork |
| Syscalls | identity and permission syscalls |
| debug_bins | `cmd_sudo`, `cmd_whoami` via syscalls |

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
4. `vfs_utimens` stub — does not apply real timestamps.
5. Sticky bit / ACLs not implemented.

## 9. Debugging tips

- `whoami`, `id` debug commands show effective ids.
- `-EACCES` on traverse: missing execute on directory component.
- `-EPERM` on mount/chown: need root euid.

## 10. Future roadmap

- Capability set and `setresuid` parity.
- `/etc/passwd` database, shadow passwords.
- setuid bit on exec.
- ACLs on tmpfs/minix (noted in IR0-vfs §10).
- Signal send permissions.
- Namespaces — not planned.

See: `IR0-vfs`, `IR0-process`, `IR0-syscalls`.
