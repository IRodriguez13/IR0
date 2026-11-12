# Linux ABI conformance and bounded syscall fuzzing

IR0 implements the Linux syscall ABI. This page covers the two tools that
measure how close it is, on x86_64 and arm64:

- **Conformance harness**: a curated set of syscall cases, plus LTP
  testcases when an LTP checkout is available. Each result is classified
  PASS / FAIL / TCONF / BROK and compared against a checked-in baseline.
- **Bounded syscall fuzzer**: deterministic random-but-typed syscalls with a
  kernel health check after every batch.

Neither tool is part of `make test-fast` or `make ctr`.

## Quick start

```bash
# Objects are built next to the sources: one ISA per tree, `make clean` between.
make ir0-conformance-x86          # boots kernel-x64-userspace.iso
make clean
make ir0-conformance-arm64        # boots kernel-arm64-product.bin

make ir0-fuzz-x86 SEED=1 COUNT=2000
make ir0-fuzz-arm64 SEED=7 COUNT=5000
make ir0-fuzz-x86 SEED=6 COUNT=5000 BATCH=11 TRACE=1   # replay one batch

make ir0-selftest-red-{signals,mm}-{x86,arm64}  # red selftests (fail today)
make ir0-selftest-sock-abi-{x86,arm64}           # former red sock group, green
make ir0-selftest-fs-abi-{x86,arm64}             # former red fs group, green
```

Set `HOST_TMP=$(mktemp -d)` to keep the logs and summaries private. Every
guest boots through `scripts/ir0_conformance.py` →
`scripts/smoke_qemu_run.sh` with a 256 MiB guest, a hard timeout, a
stale-output window and a process-group kill. The scripts never read a whole
log into memory.

| Output | Where |
|--------|-------|
| Serial log | `$(HOST_TMP)/ir0-conformance-<isa>.log`, `ir0-fuzz-<isa>-s<seed>.log` |
| Machine-readable summary | `$(HOST_TMP)/ir0-conformance-<isa>.json`, `ir0-fuzz-<isa>-s<seed>.json` |

## Conformance harness

### Layout

| Path | Role |
|------|------|
| `tests/conformance/cases_{fs,proc,mm,sock}.c` | Cases. One function per case, expected values from Linux |
| `tests/conformance/cases.h` | Case list in run order; the names are the stable ids |
| `tests/conformance/conformance.c` | Guest runner (`/sbin/init`): forks each case into a fresh `/tmp` directory, bounds it (10 s), prints KTAP |
| `tests/conformance/rt.{c,h}` | Freestanding runtime: raw syscalls returning `-errno`, signal trampoline, output. musl headers supply the per-ISA constants and layouts; arm64 links no libc |
| `tests/conformance/baseline-<isa>.txt` | Known non-PASS cases with the reason for each |
| `tests/conformance/ltp-cases.txt` | LTP testcases for the same areas |
| `scripts/ir0_conformance.py` | Host side: image, boot, classification, baseline comparison, LTP build |
| `scripts/make/conformance.mk` | Make targets |
| `tests/host/test_conformance_judge.py` | Host contract for the classification |

### Classification

| Status | KTAP line | Meaning |
|--------|-----------|---------|
| PASS | `ok N name` | Linux behavior |
| FAIL | `not ok N name` + `# at line L: <call>` / `# got=` / `# want=` | Different return value, errno or side effect |
| TCONF | `ok N name # SKIP TCONF` | A syscall returned `-ENOSYS` (not implemented yet) |
| BROK | `not ok N name # ERROR BROK <why>` | The case crashed, hung (killed after 10 s), or the guest stopped before reporting it |

The verdict compares every case with its baseline line. Unlisted cases must
PASS.

- A case that gets worse is a **REGRESSION** and fails the target. Getting
  worse means PASS becoming FAIL, TCONF or BROK, or any case becoming BROK.
- A case listed as FAIL that now reports TCONF, or the reverse, is shown as
  **CHANGED**.
- A listed case that now passes is shown as **FIXED**. Remove its line in the
  same commit as the fix.
- `--strict` also fails on FIXED and CHANGED.
- A baseline line that names no case is **STALE** and fails the target.
- The run must reach `IR0_CONFORMANCE_DONE`. A kernel panic after the done
  tag (for example when PID 1 exits) is recorded but does not change the
  verdict.

Every case was checked against Linux. The x86_64 runner, run as an ordinary
process on a Linux 7.0 host, passes 99/99; the red selftests pass there too.
Re-check new cases the same way:

```bash
make -C tests/conformance ARCH=x86_64 conformance
d=$(mktemp -d); build/conformance/x86_64/conformance "$d"   # must print pass=N fail=0
```

### Adding a case

1. Write `int <area>_<what>(void)` in `tests/conformance/cases_<area>.c` with
   `EXPECT(call, want)`, `EXPECT_ERR(call, errno)`, `OK(var, call)` and
   `CHECK(cond)`. Use raw syscalls (`S0`..`S6`) and only syscalls that exist
   on both ISAs (the `*at` forms, `pipe2`, `dup3`, `clone` for fork).
2. Add `CONF_CASE(<area>_<what>)` to `tests/conformance/cases.h`.
3. Run it on a Linux host as shown above; it must PASS there.
4. Run `make ir0-conformance-x86` and `make ir0-conformance-arm64`. If IR0
   diverges, add the case to both baselines with the reason. For a real
   kernel bug, also add a red selftest (below).

Cases with system-wide effects, such as `kill(-1, ...)`, must check
`conf_guest` and report TCONF outside the guest.

The arm64 image must fit one IR0 tmpfs file (64 KiB, `fs/tmpfs.c`), so arm64
builds with `-DCONF_TERSE`: diagnostics name the source line, not the
expression. The CPIO carries a stub `/init`, because the early rootfs binds a
CPIO only when `/init` is an ELF image, and the runner as `/sbin/init`.

### LTP

LTP is not in the repository and the targets never download it. With an
existing checkout:

```bash
git clone https://github.com/linux-test-project/ltp.git ~/src/ltp   # once, by hand
make ir0-conformance-x86 LTP_SRC=$HOME/src/ltp
```

`ir0_conformance.py ltp-build` copies `LTP_SRC` into a private temp tree,
leaving the checkout unmodified. It configures LTP with `x86_64-linux-musl-gcc
-static` and builds `lib/` plus the directories in `ltp-cases.txt`. The
binaries and a manifest go into `/ltp` on the guest disk. After its own cases
the runner execs each `/ltp/bin/<name>` with `TMPDIR` set to a fresh
directory, bounds it to 60 s, and maps the LTP exit status: 0 PASS, 32 TCONF,
bit 2 (TBROK) BROK, bit 1 (TFAIL) FAIL, a signal or a timeout BROK. Results
appear as `ltp_<name>` and use the same baseline file.

Status: the guest side was checked on x86_64 with stand-in binaries (exit 0,
1, 2, 32 and a SIGSEGV gave PASS, FAIL, BROK, TCONF, BROK). The build step
is untested because no LTP checkout exists on the development machine.
LTP needs `/dev/shm` for its result page and musl may lack pieces it expects,
so expect BROK until those exist. arm64 runs IR0 cases only: the arm64
early rootfs holds 128 KiB and tmpfs files are capped at 64 KiB.

## Bounded syscall fuzzer

`tests/conformance/fuzz.c`, built per `SEED`/`COUNT` (`BATCH`/`TRACE` for
replay), runs as `/sbin/init`.

- **Typed arguments**:
  - **fds**: pooled files, directories, pipes and sockets, plus -1, -100,
    `INT_MIN`, stale numbers. Never 0–2.
  - **Pointers**: NULL, 1, the kernel half, IR0 supervisor pages (x86_64
    `0x500000`, arm64 `0x40800000`), non-canonical addresses, -1, a
    read-only page, a buffer straddling an unmapped hole.
  - **Lengths and offsets**: 0, page edges, 2^30, 2^40, `SIZE_MAX`, negative.
  - **Other**: fuzzed flags, modes, signals, clock ids, sockaddrs, iovecs,
    timespecs and sigactions.
  - Unassigned syscall numbers (512–999).
- **Containment**: each batch of 100 calls runs in a forked child in its own
  session and scratch directory, removed after the batch. Paths are relative
  names inside that directory or read-only system files. The child never
  blocks: fds are non-blocking, `WNOHANG`, `MSG_DONTWAIT`, zero `ppoll`
  timeout. It cannot destroy the harness: no `exit`, `clone` or `execve`; it
  may not unmap its own buffers, and its report pipe is protected.
- **Health check after every batch**: the child is reaped (SIGKILL after 4 s),
  `getpid()` of init, a pipe round trip, mmap/munmap, a monotonic clock that
  advances, and a fork/wait canary. A failure prints `IR0_FUZZ_HEALTH_LOST`.
- **Oracles**: an unassigned syscall must return `-ENOSYS`. No call may leak
  an internal error code (-4095..-134, e.g. `ERESTARTSYS`).
- **Deterministic**: batch *b* of seed *s* is always the same sequence. The
  log names the last call of a batch whose child crashed or hung. A guest
  that stops responding is replayed with `BATCH=<b> TRACE=1`, which prints
  every call before and after it runs.
- **Bounds**: a wall-clock budget (`FUZZ_MAX_SEC`, 120 s), the runner's
  stale-output window (40 s) and hard timeout (600 s).

Fuzzer runs are not added to `make ctr`: ctr boots no guest today, and the
kill(-1) and kernel-page bugs below hang most seeds on arm64 and half of
them on x86_64. Once those are fixed, `make ir0-fuzz-x86 SEED=1 COUNT=2000`
(about 11 s of guest time) is a candidate smoke.

## Results at dev 629036ef

### Conformance (99 IR0 cases)

| ISA | PASS | FAIL | TCONF | BROK |
|-----|------|------|-------|------|
| Linux 7.0 host (x86_64, reference) | 99 | 0 | 0 | 0 |
| IR0 x86_64 | 60 | 27 | 12 | 0 |
| IR0 arm64 | 54 | 33 | 12 | 0 |

TCONF (ENOSYS) on both ISAs: `pread64`/`pwrite64`, `ftruncate`, `linkat`,
`statx`, `rt_sigpending`, `clock_getres`, `madvise`, and file-backed `mmap`.
The tables below list every FAIL.

### Fuzzer

| ISA | Seed / count | Result |
|-----|--------------|--------|
| x86_64 | 1 / 2000, 2 / 5000, 4 / 5000 | Healthy. No leaked errno, every unassigned syscall `-ENOSYS` |
| x86_64 | 3, 5, 6 / 5000 | Guest stops after `kill(-1, sig)` (bug K1) |
| arm64 | 1, 3 / 5000 | Guest stops after `munmap(0x40800000, ...)` (bug M9) |
| arm64 | 2 / 5000 | Guest stops after `mmap(0x40800000, MAP_FIXED, ...)` (bug M9) |
| arm64 | 6 / 5000 | Guest stops after `kill(-1, sig)` (bug K1) |
| arm64 | 4, 5 / 5000, 7 / 2000 | Guest stops (not yet replayed; replay with `BATCH`/`TRACE`) |

Other observations, not kernel bugs on their own:

- **Disk fills on x86_64**: before per-batch cleanup existed, huge
  `ftruncate`/`pwrite` offsets filled the x86_64 root (`ENOSPC`). IR0's
  filesystem does not keep sparse files sparse the way Linux tmpfs and minix
  do.
- **Smaller resource limits**: `RLIMIT_NOFILE` is 64 (Linux 1024), the pipe
  capacity is 4096 bytes (Linux 65536), and tmpfs files are capped at 64 KiB
  (`fs/tmpfs.c` `TMPFS_MAX_FILE_SIZE`).

## Kernel bugs found

Every bug has a red KTAP case under `tests/selftests/kernel/linux_abi_*.c`.
Each case passes on Linux and fails on IR0 at 629036ef. They run only
through `make ir0-selftest-red-<group>-<isa>` and are not wired into any
green gate. A case whose last line says it hangs the guest is listed last
in its group, so it cannot hide the others.

| # | Syscall | Case (conformance / red) | Linux | IR0 | ISA | Suspected file |
|---|---------|--------------------------|-------|-----|-----|----------------|
| F1 | openat, newfstatat | fs_open_errors, stat_empty_path / fs_empty_path_enoent | `""` → ENOENT | EINVAL | both | kernel/syscalls/fs_syscalls.c `sys_openat`, fs_meta.c `sys_newfstatat` (path copy) |
| F2 | mkdirat | dir_mkdir_rmdir / fs_mkdir_missing_parent_enoent | missing parent → ENOENT | EACCES | both | kernel/syscalls/fs_path_syscalls.c `sys_mkdirat` |
| F3 | lseek | fs_lseek_whence / fs_lseek_pipe_espipe | pipe → ESPIPE | 0 | both | kernel/syscalls/io_syscalls.c `sys_lseek` |
| F4 | fcntl F_SETFL | fd_fcntl_flags / fs_setfl_append_sticks | O_APPEND settable | dropped | both | kernel/syscalls/io_syscalls.c `sys_fcntl` |
| F5 | getdents64 | dir_getdents64 / fs_getdents64_lists_dot_entries | lists `.` and `..` | omitted | both | kernel/syscalls/fs_dirent.c `sys_getdents64` |
| F6 | symlinkat + fstatat NOFOLLOW | stat_symlink / fs_lstat_symlink | S_ISLNK | ENOENT | both | kernel/syscalls/fs_meta.c `sys_symlinkat`, `sys_newfstatat` |
| F7 | getcwd | dir_chdir_getcwd / fs_getcwd_counts_nul | returns strlen+1 | strlen | both | kernel/syscalls/fs_path_syscalls.c:690 `sys_getcwd` |
| F8 | execve | proc_exec_errors / fs_execve_non_executable_eacces | file without x bit → EACCES | ENOENT | both | kernel/syscalls/process_syscalls.c `sys_execveat` |
| F9 | fstat / fstatat | stat_fstat_regular, stat_umask, stat_fifo_and_dir, dir_openat_dirfd / fs_stat_layout_matches_isa | asm-generic `struct stat` on arm64 | x86_64 layout (mode reads as nlink) | arm64 | includes/ir0/stat.h (one layout for both ISAs) |
| F10 | openat, newfstatat | fs_open_errors, stat_empty_path / fs_file_component_enotdir | regular file as a path component → ENOTDIR | ENOENT | both | fs/vfs_namei.c `vfs_stat`, fs/vfs_file.c `vfs_open` |
| F11 | unlinkat AT_REMOVEDIR | dir_mkdir_rmdir / fs_rmdir_nonempty_enotempty | non-empty → ENOTEMPTY | tree removed | both | kernel/syscalls/fs_meta.c `sys_unlinkat` |
| F12 | execve | — / fs_execve_long_arg, fs_execve_e2big | strings up to MAX_ARG_STRLEN; overflow → E2BIG | 256-byte copies, EFAULT | both | kernel/syscalls/process_syscalls.c `sys_exec` |
| F13 | openat O_NOFOLLOW | stat_symlink / fs_open_nofollow_eloop | symlink → ELOOP | opens the target | both | kernel/syscalls/fs_syscalls.c `sys_open_vfs_resolved` |
| F14 | openat O_DIRECTORY | fs_open_errors / fs_open_directory_on_file_enotdir | arm64 values (040000) → ENOTDIR | x86_64 values, flag lost | arm64 | includes/ir0/open_flags.h |
| F15 | unlinkat, openat on a directory | fs_open_errors, dir_mkdir_rmdir / fs_unlink_dir_eisdir, fs_open_dir_for_write_eisdir | EISDIR | tree removed / fd / replaced by a file | both | kernel/syscalls/fs_meta.c, fs/vfs_file.c |
| S1 | rt_sigprocmask | sig_kill_stop_uncatchable / sig_kill_stop_not_blockable | SIGKILL/SIGSTOP never blocked | blocked | both | kernel/syscalls/process_syscalls.c `sys_rt_sigprocmask` |
| S2 | rt_sigaction SA_RESETHAND | sig_resethand / sig_sa_resethand_resets | handler is SIG_DFL after delivery | old handler reported | both | kernel/signals.c:248 vs `sys_rt_sigaction` storage |
| S3 | rt_sigsuspend | sig_sigsuspend / sig_sigsuspend_returns_eintr | -EINTR | 0 | both | kernel/syscalls/process_syscalls.c `sys_rt_sigsuspend` |
| S4 | nanosleep | sig_nanosleep_eintr / sig_nanosleep_eintr_from_other_process | caught signal from another process → EINTR | sleeps the full time | both | kernel/syscalls/io_syscalls.c `sys_nanosleep` (wakeup on signal) |
| S5 | wait4 | proc_wait_echild / sig_wait4_bad_options_einval | bad options → EINVAL | ECHILD | both | kernel/syscalls/process_syscalls.c `sys_wait4` |
| S6 | signal delivery | sig_handler_runs, sig_sigchld_on_exit / sig_handler_gets_signum | handler x0 = signal number | 0 | arm64 | arch/arm64 signal frame setup (`signal_redirect_irq_frame`) |
| K1 | kill(-1, sig) | fuzzer / sig_kill_all_spares_caller | skips init and the caller | signals the caller, then the guest stops | both | kernel/syscalls/process_syscalls.c:1606-1620 `sys_kill` (walks every process, incl. the caller and kernel tasks) |
| M1 | mmap | mm_mmap_einval / mm_mmap_requires_private_or_shared | no MAP_PRIVATE/SHARED → EINVAL | succeeds | both | kernel/syscalls/mm_syscalls.c `sys_mmap` |
| M2 | mmap | mm_mmap_huge_enomem / mm_mmap_huge_enomem | 1 TiB → ENOMEM | EINVAL | both | kernel/syscalls/mm_syscalls.c `sys_mmap` |
| M3 | munmap | mm_munmap_then_segv / mm_munmap_hole_succeeds | unmapped range → 0 | EINVAL | both | kernel/syscalls/mm_vma.c |
| M4 | mprotect PROT_NONE | mm_mprotect_readonly / mm_prot_none_faults | read faults (SIGSEGV) | readable | both | kernel/syscalls/mm_syscalls.c `sys_mprotect` / PTE encoding |
| M5 | mmap MAP_SHARED anon | mm_shared_anon_fork / mm_shared_anon_survives_fork | shared with the child | copied (COW) | both | mm/paging_fork.c (fork copies shared VMAs as private) |
| M6 | clock_gettime | time_clock_gettime / time_clock_process_cputime | CLOCK_PROCESS_CPUTIME_ID works | EINVAL | both | kernel/syscalls/time_syscalls.c:140 |
| M7 | clock_gettime | time_clock_gettime / time_realtime_from_rtc | REALTIME from RTC | near 0 | arm64 | arch/arm64/sources/rtc.c, drivers/timer/clock_system.c |
| M8 | munmap of an ELF text page | mm_unmapped_segv_handler_kills_task | page unmapped; a fault whose handler is gone kills with SIGSEGV | munmap fails (child exit 60), the handler keeps running | both | kernel/syscalls/mm_vma.c (ELF image pages are not unmappable VMAs) |
| M9 | mmap MAP_FIXED, munmap | fuzzer / mm_mmap_fixed_over_kernel_pages_is_harmless | plain user range | MAP_FIXED over the identity-mapped kernel heap (0x40800000) stops the guest; so does the fuzzer's `munmap(0x40800000, 1 GiB)` (replay: `make ir0-fuzz-arm64 SEED=3 COUNT=5000 BATCH=5 TRACE=1`, not reduced to a red case yet) | arm64 | kernel/syscalls/mm_syscalls.c `sys_mmap`, kernel/syscalls/mm_vma.c (no check against supervisor mappings) |
| N1 | socket | sock_create_errors / sock_bad_family_eafnosupport | unknown family → EAFNOSUPPORT | EPROTOTYPE | both | kernel/syscalls/socket_syscalls.c:210 |
| N2 | sendto | sock_not_a_socket / sock_send_non_socket_enotsock | non-socket fd → ENOTSOCK | EINVAL | both | kernel/syscalls/socket_syscalls.c `sys_sendto` |
| N3 | bind | sock_bind_errors / sock_bind_twice_einval | bound socket → EINVAL | 0 | both | kernel/syscalls/socket_syscalls.c:290 |
| N4 | connect | sock_connect_errors / sock_connect_missing_path_enoent | missing path → ENOENT | ECONNREFUSED | both | kernel/syscalls/socket_syscalls.c:708 |
| N5 | getsockname | sock_getsockname / sock_getsockname_returns_path | bound path | other path | both | kernel/syscalls/socket_syscalls.c:1490 |
| N6 | socketpair/bind SOCK_DGRAM | sock_pair_dgram_boundaries, sock_dgram_bound_sendto / sock_unix_dgram_supported, sock_unix_dgram_send_recv, sock_unix_dgram_msg | AF_UNIX datagrams | EPROTOTYPE / EINVAL | both | kernel/syscalls/socket_syscalls.c (no AF_UNIX DGRAM) |
| N7 | bind | sock_bind_errors / sock_bind_null_addr_efault | NULL address → EFAULT | EINVAL | both | kernel/syscalls/socket_syscalls.c `sys_bind` |
| N8 | bind | sock_bind_errors / sock_bind_missing_dir_enoent | missing parent directory → ENOENT | 0 | both | kernel/syscalls/socket_syscalls.c `sys_bind` |
| N9 | sendto (UDP) | — / sock_udp_send_uses_peer | no address: connected peer, else EDESTADDRREQ | EINVAL | both | kernel/syscalls/socket_syscalls.c `sys_sendto` |
| N10 | sendto (UDP) | — / sock_udp_sendto_overrides_peer | explicit address wins over the peer | peer port, explicit IP | both | net/sock_udp.c `sock_udp_sendto` |

### Red/green workflow for the owner of a bug

```bash
make ir0-selftest-red-<group>-<isa>      # red today: the case is "not ok"
# fix the kernel
make ir0-selftest-red-<group>-<isa>      # the case is "ok"
make ir0-conformance-<isa>               # reports FIXED: drop the baseline line
```

When every case of a red group passes on both ISAs, the group can move into
the regular selftest gates.

F1-F15 are fixed on both ISAs: their cases form the green group
`make ir0-selftest-fs-abi-{x86,arm64}`, and their conformance cases left
the baselines.
