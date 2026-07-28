# Porting software to ISD (IR0 userspace)

| Field | Value |
|-------|-------|
| Version | 0.1 |
| IR0 phase | T1 |
| Status | stable |
| Depends on | userspace, syscalls, tty, vfs |
| Man page | IR0-port (section 7) |
| Primary sources | `Documentation/USERSPACE.md`, `IR0-userspace/`, `setup/pid1/`, TinyCC under `/lib/tcc` |

> **Last verified:** 2026-07-28

## 1. Overview

**ISD** is the IR0 Software Distribution: BusyBox + runit + musl-linked
tools on a MINIX rootfs, booted under the IR0 kernel. This page is a
practical checklist for bringing a small C program (or BusyBox applet
workflow) onto the guest.

Out of scope: full glibc desktop ports, dynamic ELF without musl, or
claiming Linux LTP parity.

## 2. What works today

| Capability | Notes |
|------------|-------|
| Static musl / TinyCC | `tcc -B/lib/tcc -static -Os …` |
| BusyBox ash + applets | See `busybox --list` |
| Files | MINIX v1 root; **names ≤14 characters** |
| Editor | `vi` / `nano` (if injected) |
| Kernel sources | `/heart/dennis/src` via virtio-9p (`make run`) |
| Manuals | `man IR0-*` under `/usr/share/man/cat7/` |

## 3. Compile on the guest

```text
cd /home/ivan/Escritorio/code   # or any writable dir
cat > hello.c <<'EOF'
#include <stdio.h>
int main(void) { puts("hola"); return 0; }
EOF
tcc -B/lib/tcc -static -Os -o hello hello.c
./hello
```

Rules:

- Always pass `-B/lib/tcc` and prefer `-static`. Bare `tcc a.c -o a`
  may emit a dynamic ELF that SEGV on IR0.
- Keep object/binary basenames short if writing under MINIX paths
  (`hello`, not `hello-world-debug`).

## 4. Bring a host project in

**Preferred (long names OK):** edit the host tree through Dennis:

```text
ls /heart/dennis/src/kernel/main.c
# host path is the IR0 repo (QEMU -virtfs mount_tag=dennis)
```

Stage1 mounts this as root before login. If you only see `hello.c`,
the 9p device is missing (`IR0_DENNIS_9P=0` or no virtio-9p).

**Offline disk samples:** `/heart/dennis/src/hello.c` + `Makefile`
(short names only).

## 5. Syscall / libc expectations

Target **musl Linux x86-64 ABI** as implemented by IR0:

- Process: `fork`/`clone`, `execve`, `wait4`, signals (basic)
- FS: `open`/`read`/`write`/`close`, `stat`, pipes, poll
- Time: `nanosleep`, `clock_gettime` (subset)
- Net: sockets exist; raw ICMP/`ping` may still be incomplete

When something fails, compare with Linux ground-truth contracts in the
kernel tree (`scripts/linux_abi/`) rather than guessing errno.

## 6. Terminal and graphics apps

- Line editors (`vi`, `less`/`more`, `man`) need a working TTY
  (`TIOCGWINSZ`, cooked mode). Prefer `more` for paging if `less`
  misbehaves.
- Fullscreen apps (Doom-class): `/dev/fb0` + `/dev/events0`. While
  `events0` is open, keyboard EV_KEY goes to the app — not the shell.

## 7. Checklist before you claim “ported”

1. Builds with `tcc -B/lib/tcc -static` (or musl-gcc on the host, then inject).
2. Runs under ash without SEGV; ash prints signal names on fatal faults.
3. Does not require glibc-only APIs or `/proc` layouts IR0 lacks.
4. Paths fit MINIX if stored on the root disk (or live under 9p).
5. Documented with a one-line smoke or `man` note if it becomes product.

## 8. See also

- `man IR0-uspace` — product userspace / ISD layout
- `man IR0-syscalls` — syscall surface
- `man IR0-onboard` — first clone / first boot
- Host: `Documentation/USERSPACE.md`, sibling `IR0-userspace/`
