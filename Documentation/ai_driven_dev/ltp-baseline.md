# LTP syscalls baseline (SMP entry gate, criterion 4)

Measured on dev 98f3972f plus the two `ltp_harness.py` fixes, 361 listed tests
(242 + 119 in two disks), `--test-timeout 30`. Data: `scripts/smp_entry_gate/ltp_results.tsv`.
Every non-PASS row is untriaged (no `red:` / `approved:` tag). Gate line:
`4 ltp FAIL 332 BROK, 6 FAIL, 2 PANIC, 18 PASS, 5 TCONF`.

| ISA | PASS | FAIL | BROK | TCONF | PANIC/HANG |
|---|---|---|---|---|---|
| x86_64 | 18 | 6 | 332 | 5 | 0 |
| arm64 | 0 | 0 | 0 | 0 | 2 (boot, see below) |

The x86_64 number is dominated by two environmental causes, not by 332
independent bugs. Fixing them is the prerequisite for a meaningful baseline.

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

## x86_64 BROK causes (332)

| Cause | Tests | Syscall / area | Suspected cause |
|---|---|---|---|
| `open(/tmp/LTP_x/ltp_<test>_N)` ENAMETOOLONG | ~185 | open on the MINIX root | LTP creates names over 14 chars; MINIX v1 caps names at 14. /tmp lives on the MINIX root, no tmpfs. Environment, not Linux ABI |
| `mmap(0,4096,RW,MAP_SHARED,fd,0)` ENOSYS | 141 | mmap | file-backed MAP_SHARED is unimplemented; LTP's lib maps its results page this way (tst_test.c:147) |
| `tst_device` "Failed to acquire device" | 2 (rename11, renameat01) | loop/block device | needs a loop device |
| mprotect01/03 | 2 | mprotect, open, mmap | mprotect01: EINVAL where ENOMEM expected; mmap failed |
| rt_sigaction01, kill10 | 2 | signals | kill10 floods "unexpected signal 10 from 0" (SIGUSR1 to the wrong target/group); rt_sigaction01 prints TPASS then BROK |

A trial with a tmpfs mounted on /tmp (harness change, not kept) removed the
ENAMETOOLONG cause but ended in `mkdtemp ENOSPC` in 195 tests: tmpfs capacity
or space is not reclaimed across tests. Left as a finding, not committed.

## FAIL (6) and TCONF (5)

| Test | Result | Syscall | Cause |
|---|---|---|---|
| fcntl09 | FAIL | fcntl F_SETLK | EINVAL for F_WRLCK on a regular file |
| rt_sigaction02 | FAIL | rt_sigaction | call succeeded where an error was expected (invalid input not rejected) |
| sigprocmask01 | FAIL | sigpending | pending state check got errno 38 (ENOSYS) |
| writev02, writev05, writev06 | FAIL | open (writev setup) | ENAMETOOLONG, same MINIX name cause |
| eventfd06 | TCONF | io_submit | libaio absent in the musl build |
| fork05 | TCONF | n/a | test is Intel 32-bit only |
| getrusage04 | TCONF | getrusage | precision precondition |
| openat03 | TCONF | openat O_TMPFILE | not supported |
| rt_sigprocmask01 | TCONF | rt_sigpending (127) | not supported (ENOSYS) |

## Top failing syscalls (x86_64)

mmap (MAP_SHARED file, 141), open (name length, ~188), rt_sigpending/sigpending
(2), fcntl locks (1), rt_sigaction (2), mprotect (2), kill (1), loop device (2).

## Harness fixes found while measuring

- Chunk budget exceeded smoke_autokill's 3600 s limit (rc=2, no boot).
- `--only-chunk N` renumbered the chunk to 0 and overwrote its log.
- A boot-time death is charged to the first unreached test; that is why the
  arm64 rows carry the names `access01` and `prctl08`.
