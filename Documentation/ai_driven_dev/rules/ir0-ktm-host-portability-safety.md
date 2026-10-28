<!-- IR0 AI dev rule: ir0-ktm-host-portability-safety -->
<!-- alwaysApply: true -->
<!-- description: KTM host tooling must stay UNIX-portable and must never risk host RAM, disk, or processes -->

# KTM host tooling — UNIX portability and host safety

KTM host tooling runs on the developer machine, not in the guest. A bug there
can exhaust the host's RAM or disk, or make a fresh clone unrunnable. Treat
every host script as production code.

Scope: `scripts/ktm_*`, `scripts/smoke_*`, `scripts/inject_init_minix.py`,
`scripts/make/testing.mk`, `tests/ktm/**`, and any helper they invoke.

## Portability contract (Linux, BSD, macOS)

- **Shell**: new or changed host scripts use POSIX `sh`. Bash only when
  unavoidable, with `#!/usr/bin/env bash`, working on bash 3.2 (macOS), and
  never expanding an empty array under `set -u`.
- **Python**: 3.9+, standard library only. No pip, no Linux-only module at
  import time. `subprocess` takes a list argv; never `shell=True` with data.
  Use `sys.executable`, not a hardcoded `python3`.
- **No GNU-only flags**. Portable replacements:

  | Avoid | Use |
  |-------|-----|
  | `sed -i` | write a temp file and `mv`, or Python |
  | `readlink -f` | `cd` + `pwd -P`, or `Path.resolve()` |
  | `stat -c` | `stat -f` fallback, or Python `stat()` |
  | `nproc` | `getconf _NPROCESSORS_ONLN`, `sysctl -n hw.ncpu` |
  | `free` | `/proc/meminfo`, `sysctl hw.memsize`, or `os.sysconf` |
  | `sha256sum` | `shasum -a 256`, `openssl dgst -sha256` |
  | `timeout` | Python `subprocess` timeout |
  | `flock` | Python `fcntl` |
  | `find -printf` | `find -print` + Python |
  | `grep -P` | `grep -E` |
  | `cp --reflink` | plain `cp` fallback |

- **Temp files**: `mktemp "${TMPDIR:-/tmp}/name.XXXXXX"` with the X's last
  (BSD `mktemp` rejects suffixes). Python uses `mkstemp`/`mkdtemp`; never
  `tempfile.mktemp`.
- **Paths**: no `/home/...` or hardcoded `/tmp` in new code. Resolve the repo
  root from the script path, never from the caller's cwd. Reject `..` in
  paths derived from CLI input.
- **Discovery**: `command -v` / `shutil.which`; fail with the exact command to
  install or build the missing tool. Never silently substitute.
- **Locale/time**: `LC_ALL=C`, `TZ=UTC` for parsing and tests.

## Host safety contract (RAM, disk, processes)

- **QEMU memory is explicit and capped.** Every run goes through
  `scripts/smoke_qemu_run.sh` / `smoke_autokill.py`, `ktm_userdev_runner.py`, or
  `ktm_runner.py`. The runner passes `-m` (128 MiB for smokes, 256 MiB
  for userdev pilots) and rejects anything above 2 GiB or 25% of host RAM,
  whichever is lower. It refuses to start when guest RAM + 512 MiB headroom
  exceeds available host memory.
- **Never read a whole log into memory.** Stream from the last offset with a
  fixed buffer (<= 1 MiB), keep only a bounded tail (`deque(maxlen=...)`), and
  fail at `--max-log-mib` (default 64) instead of growing without bound.
- **No unbounded waits.** Every run has a hard timeout (explicit `--timeout`,
  a `--profile`, or the 180 s default) and a stale-log window.
  `start_new_session=True` plus process-group kill on every exit path.
- **Temp state is owned and cleaned.** Disks and 9p shares come from
  `mkstemp`/`mkdtemp` and are removed in `finally`. Never mutate the caller's
  `--disk` or `--share` unless explicitly requested.
- **Confinement.** `--host-file` must resolve inside the share. 9p shares are
  dedicated temp dirs, never `/`, `$HOME`, or the repo root. No host mounts,
  loop devices, or root execution.
- **Disk caps.** Images are sparse and size-capped; no `dd` without `bs` and
  `count`.

## Fresh-clone contract

- `git clone && make <target>` works without `build/`, `toolchain/`,
  `disk.img`, cached ISD artifacts, or a previous run. A target either builds
  its dependencies or fails with the exact command to run.
- No network during tests, no `~/.cache`, dotfiles, SSH keys, or user paths.
- `make -n` must not execute side effects. Recursive `$(MAKE)` lines are the
  documented exception; keep them `+@`.
- Host dependencies are discovered through `scripts/ensure-host-deps.sh` /
  `scripts/deptest.sh`; keep apt, pacman, dnf, zypper, brew, and pkg paths
  working.

## Enforcement

- `make host-portability-guard` (part of `ktm-check`) scans host tooling for
  banned patterns and exercises the QEMU memory/log helpers.
- Existing violations live in `scripts/host_portability_baseline.txt`; the
  ledger only shrinks. New violations fail.
- Review checklist: shebang, temp template, explicit `-m`, bounded log,
  cleanup trap, no GNU-only flag, and a fresh-clone run of the touched target.
