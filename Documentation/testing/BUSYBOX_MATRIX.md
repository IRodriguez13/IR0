# BusyBox applet matrix — capture contract

> **Última verificación:** 2026-07-28  
> **Fuente de verdad:** `IR0-userspace/smoke/busybox_matrix_smoke.c`,  
> `IR0-userspace/smoke/matrix_capture.c`, `make busybox-matrix`

## Scope

- **52** applet cases (real `execve("/bin/busybox", …)` in the guest).
- Classification: `supported` / `partial` / `unavailable`.
- Host aggregator: `IR0-userspace/scripts/busybox_applet_matrix.py` → `bb_status.tsv`.

## QEMU / memory layout

| Item | Value |
|------|--------|
| Target | `make busybox-matrix` |
| RAM | `-m 1024M` |
| PMM pool | physical `[32 MiB, 512 MiB)` |
| `USER_MMAP_START` | `0x20000000` |
| mmap hints | only `[USER_MMAP_START, USER_MMAP_END)` |
| Case timeout | 12000 ms wall (extends by EOF exit grace) |
| EOF exit grace | 5000 ms after first pipe EOF before SIGKILL |
| Post-exit drain | up to 2000 ms until pipe EOF |
| Store cap | 1024 bytes (matcher continues past store) |

Do **not** reintroduce post-fork anonymous `mmap` for the parent capture
buffer: a prior experiment returned a VA outside the arena (~`0x809c7xxx`) and
SEGV'd PID 1 on null-termination.

## Lifecycle (required)

```text
spawn worker
  → nonblocking read while alive (poll timeout=0 only)
  → waitpid(WNOHANG) may observe exit
  → on case timeout: SIGKILL → reap (EXIT_CLOSE drops writers)
  → drain until read()==0 (EOF)   ← never stop on EAGAIN after exit
  → waitpid if needed
  → classify
```

**Do not** use `poll(fd, timeout>0)` in the matrix parent: IR0 `sys_poll`
allocates from a global `poll_waiter` pool (`MAX_POLL_WAITERS=16`). Exhaustion
returns `-EAGAIN`; treating that as end-of-drain caused false `no-eof` /
lost needles under stress. Sleep via `poll(NULL, 0, ms)` instead.

**Root cause of `reason=output` flake (exit 0, needle missing):** after
`waitpid` the parent drained with `O_NONBLOCK` and treated `EAGAIN` as
end-of-capture, dropping late pipe bytes (hypothesis B). Functional exit was
0; the needle was produced but not observed.

**Exit-stall remap:** if the pipe already reached EOF and the needle (if any)
matched with `want_ec==0`, but the worker was SIGKILL'd after the deadline,
the harness records `ec=0` (not `reason=timeout`). PASS/FAIL pairs showed
identical `bytes`/`hash`/`eof`; only wait status differed. Guest exit-after-
close stalls remain an open debt — missing needles still fail as `reason=output`.

## Protocol (control on init stdout / serial)

Markers use `write(1, …)` only (no stdio buffering):

```text
BBCASE_BEGIN seq=N name=<applet>
BBCASE_END seq=N name=… result=PASS|FAIL status=… reason=… ec=…
  bytes=… hash=… eof=0|1 truncated=0|1 reaped=0|1 needle=0|1
  functional=0|1 output=0|1 capture=0|1
BBMATRIX_TOTAL cases=52 supported=… partial=… unavailable=…
BBMATRIX_END result=PASS|FAIL passed=… total=52
BBMATRIX_OK          # only if all 52 supported AND capture integrity held
BBMATRIX_FAIL …      # otherwise
```

Legacy lines `BBMATRIX applet=…` remain for `busybox_applet_matrix.py`.

### Result axes

| Axis | Meaning |
|------|---------|
| `functional` | exit code matches `want_ec` |
| `output` | streaming needle found (if any) |
| `capture` | EOF seen, worker reaped, no capture-timeout |
| matrix protocol | `BBMATRIX_END` / `BBMATRIX_OK` only after all cases closed |

`reason=output` is reserved for a real missing needle after a clean capture.
Capture races use `no-eof`, `capture-timeout`, or `no-reap` — never disguised
as `output`.

## Needles

- Validated with a streaming matcher (cross-chunk safe); not `strstr` on
  unterminated chunks.
- Store may truncate at 1024 bytes (`truncated=1`); matcher still sees the
  full stream via discard reads.
- Needles are stable literals (`Usage:`, `uid=0`, `IR0`, …). Do not weaken
  them globally to hide capture bugs.

## Host regressions

`tests/host/test_matrix_capture.c` covers split needles, truncate+match,
drain-to-EOF after writer exit, POLLHUP with pending data, empty EOF, no
newline.

## Stress

Record consecutive `make busybox-matrix` runs under
`out/busybox-matrix/stress*/SUMMARY.txt` after each oleada.

**Prior (capture flake oleada baseline):** 6/6 functional completion
(`BBMATRIX_OK`), 4/6 complete capture classification, 2/6 `reason=output`,
0/6 SEGV/panic.

**After this oleada (`out/busybox-matrix/stress10/`):** 30/30 consecutive
`BBMATRIX_END result=PASS`, `supported=52`, `BBMATRIX_OK`, `eof=1` and
`reaped=1` on all 52 cases, no `reason=output`, no `parent-segv`, no panic.

Gate: no `reason=output`, no `parent-segv`, no panic, `BBMATRIX_END result=PASS`.
Do not claim “100% stable” beyond the stress actually executed.

## Still partial (out of scope for this capture oleada)

- `MS_BIND`
- Extra `umount2` flags
- General mount `data=` parsing
- Guest: `busybox grep --help` can stall after a short write on IR0 (matrix
  uses `grep -F` instead); `id` uses `id -u` and seeds `/etc/passwd` to avoid
  passwd-db stalls. Root-cause of those guest stalls is separate.
