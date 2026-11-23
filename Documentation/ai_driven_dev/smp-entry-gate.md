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
| Q1 | OPEN | (a) Retire the early rootfs world (erfs*, early_*, product_*, ramtab, bpath/bpivot) once the common VFS/process path boots both ISAs from the block image. | 25 erfs*/early_*/product_* sources remain in kernel/syscalls/. |
| Q2 | PARTIAL | (a) Each syscall implementation lives in its owner (fs/, mm/, net/, kernel/signals/, ...); only entry and dispatch stay in kernel/entry/. | futex, signal and kexec syscalls live with their owners; the rest stay in kernel/syscalls/; no kernel/entry/ yet. |
| Q3 | OPEN | (a) Pseudo filesystems become full vfs_fstype mounts with inode and file operations; the named_* side tables become inodes of the parent filesystem. | fs/pseudo_fs_registry.c, fs/path_routed.c and fs/named_*.c remain. |
| Q4 | PARTIAL | (a) Platform device drivers live in drivers/<class>/ for both ISAs; arch/ keeps only CPU, MMU and entry code. | The arm64 GICv2, BCM2836, PL011, serial and RTC drivers are in drivers/{irq,tty,rtc}/; arch/arm64/sources/virtio_blk_early.c remains and goes with Q1. |
| Q5 | DONE | (b) Arch facades take Linux topic names (asm/io.h, asm/pgtable.h, asm/tlbflush.h, asm/processor.h); includes/ir0/ keeps only portable APIs. | No includes/ir0/arch_*.h remains (17 retired, names guarded by retired_api_names.txt); ISA hooks live in arch/{x86-64,arm64}/include/asm/{page,pgtable,processor,setup,elf,hw_breakpoint,task_context,syscall_frame,task_access}.h. |
| Q6 | PARTIAL | (b) Exec and ELF loading move to fs/exec.c and fs/binfmt_elf.c (Linux); the kernel never knows which userspace program runs. | The ELF loader is fs/binfmt_elf.c and criterion 2 finds no userspace names; execve and the rest of exec stay in kernel/exec/ (target fs/exec.c). |
| Q7 | PARTIAL | (a) The logger moves to kernel/printk/ and the logging.h aliases (lane L1) are retired. | klog is kernel/printk/klog.c; includes/ir0/logging.h remains. |
| Q8 | OPEN | Three console layers: tty, console, video driver. | DRV-3 not started; drivers/tty/ is being reworked by UPROG-1. |
| Q9 | PARTIAL | (a) mm_struct and the VMAs are owned by mm/. | mm_struct, brk, the VMA list and mmap/munmap/mprotect are in mm/; free-VA search and private file mappings stay in kernel/syscalls/mm_syscalls.c. |
| Q10 | DONE | Rename includes/ir0/wait.h (wait-status macros) to wait_status.h, freeing wait.h for wait queues (X-7). | includes/ir0/wait.h is now includes/ir0/wait_status.h; no includes of ir0/wait.h remain. |
| Q11 | PARTIAL | Target kernel/{entry,signal,process,time,irq,panic,printk} plus lib/; kernel/main.c stays and is the only loose file in kernel/. | kernel/panic/, kernel/printk/ and kernel/time/ exist; kernel/entry/ does not, and kernel/{boot,exec,futex,kexec,power,sys,syscalls,test} are outside the target list. |
| Q12 | PARTIAL | (b) Block devices are handles with one name lookup; every device class plugs into one class API with ops and feature flags, as the NIC class does. | Block devices resolve through one lookup to a dev_t handle; the *_by_name wrappers remain for old callers and other classes do not follow the NIC pattern yet. |
| Q13 | OPEN | (b) Drivers register through an explicit registration table that is part of the common driver contract. | Registration is runtime driver_bootstrap_register() calls from arch/x86-64/sources/boot_drivers.c, not a table under a common contract. |
| Q14 | PARTIAL | Delete the IR0-only syscalls (console_scroll, console_clear, keymap_set, keymap_get) in favour of Linux ioctls and escape sequences. | x86-64 no longer decodes console_scroll/clear (440/441); CSI stays on the console renderer. keymap_set/get (442/443) stay because ISD firstboot calls them. Host recount 167 over 0..__NR_syscall_max-1. |
| Q15 | PARTIAL | No weak symbols: required arch hooks fail at link time through a port-contract header; optional generic implementations are chosen explicitly by the build registry; new ISAs start from arch/template/. | arch/template/port.h lists the port hooks; weak definitions remain in arch/common/{boot_arch,boot_port,oops_port}.c, drivers/virtio/virtio_mmio.c, kernel/boot/boot_firmware_sysfs.c, mm/alloc/allocator.h and net/net_compat.c. |
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
