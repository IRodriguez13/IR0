# LTP syscalls baseline (SMP entry gate, criterion 4)

x86_64 measured on feat/map-shared-files 4062d9d4 (dev 710bf328 plus the tmpfs
reclaim and MAP_SHARED fixes) with /tmp on a tmpfs mounted by the runner, 361
listed tests (242 + 119 in two disks), `--test-timeout 30`. arm64 is still the
dev 98f3972f measurement. Data: `scripts/smp_entry_gate/ltp_results.tsv`.
Every non-PASS row is untriaged (no `red:` / `approved:` tag).

TWARN counts tests whose only non-PASS line is LTP's
`Kernel is older than minimal supported` warning, or a warning from setup
(for example `test_dev.img: ENOSYS`); their checks passed.

| ISA | PASS | TWARN | FAIL | BROK | TCONF | PANIC/HANG |
|---|---|---|---|---|---|---|
| x86_64 | 23 | 172 | 39 | 122 | 5 | 0 |
| x86_64 before (dev 98f3972f, /tmp on MINIX) | 18 | 0 | 6 | 332 | 5 | 0 |
| arm64 | 0 | 0 | 0 | 0 | 0 | 2 (boot, see below) |

The two environmental causes of the first x86_64 run are gone: /tmp is a
tmpfs (no MINIX 14-character names) whose space is reclaimed between tests, and
file-backed MAP_SHARED works, so LTP's results page maps. No test reports
`mkdtemp` ENOSPC or `mmap` ENOSYS on MAP_SHARED any more. Tests that used to
stop at those two setup steps now run and show real divergences below.

## PANIC / HANG

- arm64 chunks 0 and 1 (rows `access01`, `prctl08`): the kernel panics before
  the runner starts, `boot_init.c:167 Failed to load /sbin/init`. The log says
  "Configured root block device not detected" and falls back to a tmpfs root.
  The arm64 boot binds only a CPIO/ELF init (<= 128 KiB) from `vda`
  (`arch/arm64/sources/boot_port.c`); `CONFIG_ROOT_BLOCK_DEVICE` defaults to
  `hda` and no MINIX root mount from virtio-blk happens. Building with
  `CONFIG_ROOT_BLOCK_DEVICE="vda"` did not change this. No arm64 test ran.
  The two rows name the first test of each disk; the whole arm64 set is unmeasured.
- x86_64: no panic, no hang. All 361 tests produced a result.

## x86_64 BROK causes (122)

| Cause | Tests | Area |
|---|---|---|
| `tst_device` "Failed to acquire device" | 26 | no loop device (`test_dev.img` fallocate ENOSYS) |
| `getpwnam(nobody)` ENOENT | 20 | no /etc/passwd on the LTP disk (environment) |
| `tst_checkpoint_wait` ETIMEDOUT | 11 | futex wake through the shared checkpoint page |
| `Cannot parse kernel .config` | 10 | no /proc/config.gz or /boot/config |
| `/proc/sys/kernel/pid_max` ENOENT | 8 | procfs sysctl missing |
| `Failed to copy resource 'execve_child'` | 3 | resource copy into the test dir |
| other (utimensat ENOSYS, waitpid ECHILD, SIGSEGV, timeouts, /proc/self/maps parse) | ~44 | individual |

## x86_64 FAIL causes (39, top)

| Cause | Tests | Syscall |
|---|---|---|
| `ign_handler (1) != 0` | ~25 lines | signal: SIG_IGN reported as handler |
| child did not dump core / core bit | ~22 lines | waitpid, kill: no WCOREDUMP |
| FIFO `select` readiness | 3 lines | select |
| MAP_PRIVATE file mapping ENOSYS (mmap06), closed fd EINVAL not EBADF (mmap08) | 2 | mmap |
| PR_SET_TIMERSLACK EINVAL, getcwd EFAULT not ERANGE, fcntl09 locks | ~6 | prctl, getcwd, fcntl |

TCONF (5) is unchanged: eventfd06, fork05, getrusage04, openat03,
rt_sigprocmask01.

## Harness fixes found while measuring

- Chunk budget exceeded smoke_autokill's 3600 s limit (rc=2, no boot).
- `--only-chunk N` renumbered the chunk to 0 and overwrote its log.
- A boot-time death is charged to the first unreached test; that is why the
  arm64 rows carry the names `access01` and `prctl08`.
