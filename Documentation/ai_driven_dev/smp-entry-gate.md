# SMP entry gate

> **Decided:** 2026-11-17 by the maintainer
> **Check:** `make smp-entry-gate` (`scripts/smp_entry_gate.py`)
> **Data:** `scripts/smp_entry_gate/` (decision status, userspace-name list
> and ledger, LTP results)

SMP work starts only when the single-CPU kernel is consolidated: every
architecture debt is paid, the kernel no longer knows which userspace
program runs, the interface-review decisions are applied, and a Linux
syscall test suite runs clean on both ISAs. Until then SMP bugs would land
on top of known debt and be hard to tell apart from it.

The gate opens when **all four** criteria hold. `make smp-entry-gate`
reports each one as PASS, FAIL or UNKNOWN and exits non-zero while any of
them is not PASS. It is a planning gate, not a regression gate: it is not
part of `test-fast`, `ctr` or the pre-push order.

## What may proceed before the gate opens

- **SMP-0:** the inventory of global mutable state and its guard.
- **SMP-3 invariants:** the documentation of the locking and per-CPU
  invariants that SMP code must keep.

Everything else that makes the kernel run on more than one CPU (secondary
CPU bring-up, per-CPU run queues, locking conversions, IPIs) waits.

## Criteria

### 1. Every architecture_guard ledger is empty

`scripts/architecture_guard.py` names its debt ledgers:
`DEBT_LEDGER_FILES` (files of reviewed exceptions;
`scripts/isa_register_name_baseline.txt` and
`scripts/undefined_prototypes.txt` today) and `DEBT_LEDGER_SETS`
(in-module lists; `RAW_ATOMIC_LEDGER` and `INCLUDE_LAYOUT_BASELINE`
today). The gate counts the non-comment lines of each file and the entries
of each list; the criterion passes when the total is zero.

`scripts/retired_api_names.txt` is not counted: it is the deny-list of
retired names, which never empties. A new guard with an exception ledger
adds it to `DEBT_LEDGER_FILES` or `DEBT_LEDGER_SETS`.

### 2. UPROG-1 is done

No kernel code is justified by, or special-cases, a particular userspace
program (maintainer decision Q6: the kernel never knows which userspace
program runs; it exposes the Linux ABI). The gate greps every tracked
kernel source (`arch block cpp drivers fs includes ipc kernel ktm lib mm
net rust sched smp sound`; `.c .h .S .asm .cpp .hpp .rs`), excluding the
in-kernel tests (`kernel/test/`), KTM mocks (`ktm/mocks/`) and the copied
Linux UAPI headers (`includes/uapi/`), for the names in
`scripts/smp_entry_gate/userspace_programs.txt`: whole words,
case-insensitive, comments included, because a comment that explains
kernel behaviour by a program is the same coupling as code that tests for
it.

Reviewed false positives (ICMP echo called "ping", "ping-pong" buffers, a
retired syscall note) go to
`scripts/smp_entry_gate/userspace_programs_ledger.txt` as
`<path> <name>  # why`. An entry whose hits are gone is stale and fails
the criterion, so the ledger only shrinks. The fix for a real hit is to
justify the behaviour by the Linux ABI and its test, or delete it.

### 3. Interface-review decisions Q1–Q15 are applied

The questions are in [`interface-review.md`](interface-review.md)
section 16. The maintainer's answers and their status live in
`scripts/smp_entry_gate/decisions.tsv`; the table below is rendered from
that file (`python3 scripts/smp_entry_gate.py --sync-doc`) and the gate
fails when the two differ. The criterion passes when every decision is
DONE.

Status: **DONE** applied everywhere (and guarded where a guard is
possible), **PARTIAL** started, **OPEN** not started.

<!-- smp-entry-gate:decisions:begin -->
| Q | Status | Decision | Evidence |
|---|--------|----------|----------|
| Q1 | DONE | (a) Retire the early rootfs world (erfs*, early_*, product_*, ramtab, bpath/bpivot) once the common VFS/process path boots both ISAs from the block image. | ARM64 mounts its vda root through vfs_init_root() (root=/dev/vda) like x86 mounts hda; erfs*/early_*/product_* sources, fs/{ramtab,bpath,bpivot,bpivot_boot}.c, their headers and the syscall_context_provider layer are deleted; architecture_guard.py [retired-file] and retired_api_names.txt reject them (early_clock.h, the live pre-timer clock hook, stays). |
| Q2 | PARTIAL | (a) Each syscall implementation lives in its owner (fs/, mm/, net/, kernel/signals/, ...); only entry and dispatch stay in kernel/entry/. | kernel/entry/ holds syscall_dispatch.c and syscall_table.c; futex, signal and kexec syscalls live with their owners; fs_*, io, mm_*, process, socket, epoll, swap and time syscalls remain in kernel/syscalls/. |
| Q3 | OPEN | (a) Pseudo filesystems become full vfs_fstype mounts with inode and file operations; the named_* side tables become inodes of the parent filesystem. | fs/pseudo_fs_registry.c, fs/path_routed.c and fs/named_{devnode,fifo,socket,symlink}.c remain. |
| Q4 | PARTIAL | (a) Platform device drivers live in drivers/<class>/ for both ISAs; arch/ keeps only CPU, MMU and entry code. | GICv2, BCM2836, PL011, serial, RTC and virtio-blk drivers are in drivers/{irq,tty,rtc,virtio}/; arch/x86-64/sources/interrupt/{pic,keyboard}.c remain under arch/. |
| Q5 | DONE | (b) Arch facades take Linux topic names (asm/io.h, asm/pgtable.h, asm/tlbflush.h, asm/processor.h); includes/ir0/ keeps only portable APIs. | No arch_*.h remains under includes/ or arch/ (names guarded by scripts/retired_api_names.txt); ISA hooks live in arch/{x86-64,arm64}/include/asm/. |
| Q6 | PARTIAL | (b) Exec and ELF loading move to fs/exec.c and fs/binfmt_elf.c (Linux); the kernel never knows which userspace program runs. | fs/exec.c holds execve and fs/binfmt_elf.c the ELF loader; kernel/exec/{saved_environ,vdso}.c remain outside fs/. |
| Q7 | DONE | (a) The logger moves to kernel/printk/ and the logging.h aliases (lane L1) are retired. | klog is kernel/printk/klog.c with a console registry in kernel/printk/console.c; includes/ir0/logging.h is gone (4bd61262). |
| Q8 | PARTIAL | Three console layers: tty, console, video driver. | Steps 1-5 and 7 of console-layers.md are done (VT in drivers/tty/vt/vt.c, mirror in drivers/tty/tty_mirror.c, printk console registry); step 6 (one ONLCR policy for fd 1/2 and tty writes) is not done. |
| Q9 | PARTIAL | (a) mm_struct and the VMAs are owned by mm/. | mm_struct, brk, the VMA list, mmap/munmap/mprotect and free-VA search (mm/va_search.c) are in mm/; kernel/syscalls/{mm_syscalls,mm_brk,mm_vma}.c remain as thin entries. |
| Q10 | DONE | Rename includes/ir0/wait.h (wait-status macros) to wait_status.h, freeing wait.h for wait queues (X-7). | includes/ir0/wait.h is now includes/ir0/wait_status.h; no source includes ir0/wait.h remain. |
| Q11 | PARTIAL | Target kernel/{entry,signal,process,time,irq,panic,printk} plus lib/; kernel/main.c stays and is the only loose file in kernel/. | kernel/{entry,signals,process,time,irq,panic,printk} exist; kernel/{boot,exec,futex,kexec,power,sys,syscalls,test} are outside the target list. |
| Q12 | PARTIAL | (b) Block devices are handles with one name lookup; every device class plugs into one class API with ops and feature flags, as the NIC class does. | Block devices resolve through ir0_block_lookup_by_name to a dev_t handle with struct ir0_block_ops (block/blockdev.c); other device classes do not follow the NIC class pattern yet. |
| Q13 | DONE | (b) Drivers register through an explicit registration table that is part of the common driver contract. | struct driver_boot_table/driver_boot_entry in includes/ir0/driver_bootstrap.h; tables in drivers/init_drv.c, arch/x86-64/sources/boot_drivers.c and arch/arm64/sources/boot_arch.c run through driver_bootstrap_run(). |
| Q14 | PARTIAL | Delete the IR0-only syscalls (console_scroll, console_clear, keymap_set, keymap_get) in favour of Linux ioctls and escape sequences. | console_scroll and console_clear are gone from includes/ir0/syscall_id.h; keymap_set/get (IR0_SYSCALL_EXTENSION_KEYMAP_*) remain in kernel/entry/syscall_dispatch.c and arch/x86-64/sources/syscall_decode.c. |
| Q15 | DONE | No weak symbols: required arch hooks fail at link time through a port-contract header; optional generic implementations are chosen explicitly by the build registry; new ISAs start from arch/template/. | No weak symbols remain in kernel sources (architecture_guard.py check 35 enforces it); arch/template/port.h lists the port hooks. |
<!-- smp-entry-gate:decisions:end -->

### 4. LTP syscalls subset on both ISAs

An LTP `syscalls` subset runs on x86_64 and arm64 with no kernel panic and
no hang, and every result other than PASS is triaged: either a red case
is filed (a failing selftest or conformance case that reproduces it) or a
TCONF / known-gap entry is approved by the maintainer.

The results go to `scripts/smp_entry_gate/ltp_results.tsv`, one line per
test and ISA, tab-separated:

```text
<arch>	<test>	<result>	[<triage>]
x86_64	openat01	PASS
arm64	fcntl14	FAIL	red:tests/selftests/kernel/linux_abi_fs.c
arm64	ioperm01	TCONF	approved:maintainer 2026-12-01, no port I/O on arm64
```

`<arch>` is `x86_64` or `arm64`; `<result>` is PASS, FAIL, BROK, TWARN,
TCONF, PANIC or HANG. PANIC and HANG always fail the criterion. FAIL, BROK
and TWARN need `red:<path of the filed case>` or `approved:<who, date,
why>`; TCONF needs `approved:`. The criterion is UNKNOWN until the file
exists, and FAIL while either ISA has no results.

## Running it

```bash
make smp-entry-gate                          # summary, first ten findings each
python3 scripts/smp_entry_gate.py --verbose  # every finding
python3 scripts/smp_entry_gate.py --sync-doc # after editing decisions.tsv
```

Closing a criterion is ordinary work with its own commits and evidence;
the gate only reports. Update `decisions.tsv` (and re-render this
document) in the same commit that applies a decision.
