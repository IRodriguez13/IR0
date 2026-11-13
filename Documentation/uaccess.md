# Kernel ↔ userspace copy frontier (uaccess)

> **Last verified:** 2026-11-13  
> **Source of truth:** `includes/ir0/copy_user.h`, `mm/uaccess/copy_user.c`,  
> `mm/uaccess/user_va.h` (`mm_user_va_ok`, mm-private), `arch/*/sources/mm.c`,
> `mm/uaccess/paging_uaccess.c` (`copy_*_user_mm`, `clear_user_mm`),
> [`LINUX_SHAPED.md`](LINUX_SHAPED.md),  
> `kernel/lib/signals.c`, `scripts/architecture_guard.py`  
> (`check_usercopy_no_raw_user_touch`), `tests/host/test_usercopy_contract.c`,  
> `scripts/smoke_session_soak.py` (FATAL includes `KERNEL_UACCESS_FAULT`)

This document is the **canonical contract** for every kernel write/read of a
userspace virtual address. It exists so faults can be **classified by layer
and ISA** instead of mixing “session SEGV”, “#DF”, and “memcpy under CR3”
into one symptom.

Spanish mirror: [`esp/uaccess.md`](esp/uaccess.md).

## Why this boundary matters

A single serial line `CONSOLE_SESSION_SEGV` is **not** a root cause. After the
uaccess oleada, the tree separates three distinct failure classes:

| Class | Typical serial tag | Where to look | Not the same as |
|-------|--------------------|---------------|-----------------|
| Kernel touched user VA without COW-safe copy | `CLASSIFY KERNEL_UACCESS_FAULT` (+ `USER_FAULT_FRAME`, `cs=8`) | Caller skipped `copy_*_user` | Userspace bug |
| Genuine userspace #PF / SIGSEGV | `USER_FAULT_FRAME` with `cs=user`, or session SEGV without uaccess classify | Process / libc / ash | Kernel memcpy bug |
| Nested / switch / kstack #PF → #DF | `CLASSIFY PRIMARY_VECTOR_*`, nested halt tags | `sched/switch`, CR3 order | uaccess |

**Reproducibility goal:** if `KERNEL_UACCESS_FAULT` appears, the bug is in
the **portable copy contract or a caller that bypassed it** — not in ash, and
not (by default) in arch entry ASM. If it never appears but the shell dies,
look elsewhere (TTY, pipe, signal delivery logic *after* a successful frame
copy, etc.).

```mermaid
flowchart TB
  subgraph callers [Portable callers]
    Sys[syscalls / signals / VFS helpers]
  end
  subgraph facade [ISA-agnostic facade]
    CU["includes/ir0/copy_user.h<br/>access_ok copy_to/from_user clear_user<br/>strncpy_from_user get_user put_user"]
  end
  subgraph isa [Per-ISA VA window]
    VA["mm_user_va_ok() (mm/uaccess/user_va.h)<br/>bounds: arch/*/sources/mm.c"]
  end
  subgraph mm [Portable MM walk]
    REG["copy_*_user_mm clear_user_mm<br/>user leaves only<br/>COW break on write"]
  end
  Sys --> CU --> VA
  CU --> REG
```

## Contract (hard rules)

1. **Never** `memcpy` / `memset` / `strncpy` / direct `*` stores into a
   userspace pointer from kernel code on a production userspace path.
2. **Always** use:
   - `copy_to_user` / `copy_from_user` / `clear_user` / `strncpy_from_user`
     for the **current** mm, or
   - `copy_to_user_mm` / `copy_from_user_mm` / `clear_user_mm` only when the
     target is not the current mm (ELF load into a new root, signal frames,
     exit/wait bookkeeping of another process).
   - The copy validates its own range and returns `-EFAULT`; a syscall
     copies and returns that error, it does not call `access_ok()` first.
   - `access_ok(addr, n)` is the one public user-pointer predicate, for the
     cases where nothing is copied now (see *Where access_ok stays*).
     Mapping ranges (mmap/munmap/mprotect, brk) use the mm policy
     `mm_user_range_mappable()`; code and TLS addresses compare against
     `mm_user_va_start()` / `mm_user_va_end()`. `mm_user_va_ok()` is private to `mm/`;
     arch-guard `[uaccess-single-api]` rejects it elsewhere.
3. **Never** `load_page_directory(user_pgd)` + raw `memcpy((void *)user_va, …)`.
   That pattern caused the post-login ash kill: write to a present RO / COW PTE
   under kernel `cs=8` → `KERNEL_UACCESS_FAULT`.
4. Bounce buffers are preferred for small structs (`utsname`, `timeval`,
   `timespec`): fill on the kernel stack, then one `copy_to_user`.
5. Kernel threads (`KERNEL_MODE`: dbgshell, ktest) call syscalls with kernel
   buffers. One static rule in `mm/uaccess/copy_user.c` (`uaccess_kernel()`) lets
   `access_ok()` and the copies accept them. A process
   mode is fixed at spawn, inherited by fork, and exec requires `USER_MODE`,
   so no ring-3 pointer is ever checked under it.
6. The page walk touches only **user-accessible** leaves (`pte_user`). The
   user VA window also holds supervisor identity pages (x86-64 keyboard ring
   at `0x500000`); a syscall pointer into them is `-EFAULT`
   (`make ir0-selftest-uaccess-x86` / `-arm64`).

Header contract comment: [`includes/ir0/copy_user.h`](../includes/ir0/copy_user.h).

## Where access_ok stays

Only where nothing is copied at that point (Linux uses `access_ok` the same
way):

| Site | Why there is no copy to rely on |
|------|---------------------------------|
| `set_tid_address`, `set_robust_list` | The pointer is stored for exit time |
| `futex(FUTEX_WAKE)` | Nothing is read at `uaddr`; only the address is used |
| `epoll_wait` events (Linux `do_epoll_wait`) | The copy comes after a wait that a bad buffer must not start |
| `sendto` / datagram `recvfrom` buffer (Linux `import_ubuf`) | EFAULT precedes the fd checks; the UDP/ICMP backends dequeue before the copy |
| `sendmsg` / `recvmsg` iovecs (Linux `import_iovec`) | EFAULT before a stream receive waits for data |

Calls in `fs/` and `kernel/syscalls/fs_*.c` are handled with the file
syscalls.

Signal handler and `SA_RESTORER` addresses and the `ARCH_SET_FS` base are
code or TLS addresses of a task, not pointers: they compare against the
public bounds `mm_user_va_start()` / `mm_user_va_end()`, because
`access_ok()` passes kernel-mode callers.

## API map (portable)

| API | Role |
|-----|------|
| `access_ok(addr, n)` | The one user-pointer predicate, for no-copy cases: ISA VA window (kernel threads pass) |
| `copy_to_user` / `copy_from_user` | Current process; walks its root via `copy_*_user_mm` |
| `clear_user` | Zero user range via `clear_user_mm` |
| `strncpy_from_user(dst, src, n)` | Linux semantics: length, `n` when no NUL fits, or `-EFAULT` |
| `get_user` / `put_user` | Scalar macros over `copy_*_user` |
| `copy_to_user_mm` / `copy_from_user_mm` / `clear_user_mm` | Explicit root (Linux `access_process_vm` analogue) |

The `_mm` walks live in [`mm/uaccess/paging_uaccess.c`](../mm/paging_uaccess.c) and
perform COW-aware page walks. That is the **only** supported way to break COW
on a kernel-initiated user write.

## Multi-ISA layout (fault isolation by architecture)

| Layer | Tree path | Responsibility |
|-------|-----------|----------------|
| Contract + macros | `includes/ir0/copy_user.h` | Same API on every ISA |
| Portable glue | `mm/uaccess/copy_user.c` | Kernel-thread rule, `access_ok`, dispatch to the `_mm` walks |
| VA window | `mm_user_va_ok` in `mm/uaccess/user_va.h` (mm-private), bounds `mm_user_va_start()` / `mm_user_va_end()` in `arch/*/sources/mm.c` | ISA canonical user range |
| COW / PTE walk | `mm/uaccess/paging_uaccess.c` | User leaf check, RO / COW break |
| Arm64 early | early MM / `arm64_mmu_user_buf_ok` where still used | Must still go through `copy_*_user` names; **no COW claim** until portable MM matches |

**How to isolate an ISA bug:**

1. If `mm_user_va_ok` rejects a valid userspace range → fix the **arch** bounds (`mm_user_va_start`/`mm_user_va_end`).
2. If range OK but region walk returns `-EFAULT` / COW mishandled → fix **`mm/`**.
3. If neither: caller bypassed the facade → fix **portable caller** (`kernel/lib`, `kernel/syscalls`).
4. Arm64 early without COW: expect `-EFAULT` or incomplete write semantics until MM parity; do **not** invent a second parallel API name.

x86-64 window (code): `0x00400000` … `0x00007FFFFFFFFFFF` in
[`arch/x86-64/sources/mm.c`](../arch/x86-64/sources/mm.c).

## Callers migrated (P0, 2026-09-02)

| Area | File | Pattern now |
|------|------|-------------|
| Signal frames / `siginfo` | `kernel/signals.c` | `copy_to_user_mm(process_pgd(p), …)` |
| `uname` | `kernel/syscalls/fs_path_syscalls.c` | kernel `struct utsname` + `copy_to_user` |
| `gettimeofday` | `kernel/syscalls/time_syscalls.c` | bounce + `copy_to_user` |
| `nanosleep` rem | `kernel/syscalls/io_syscalls.c` | `copy_from_user` / `copy_to_user` |
| `getrandom` | `kernel/syscalls/process_syscalls.c` | kernel chunk + `copy_to_user` |
| mprotect zero | `kernel/syscalls/mm_syscalls.c` | `clear_user_mm` |

Paths that already bounced (`sys_read` / `sys_write`, getdents kbuf) were left
alone.

## Static enforcement (tree structure → reproducible CI)

`scripts/architecture_guard.py` → `check_usercopy_no_raw_user_touch()`:

| Guard tag | What it bans |
|-----------|--------------|
| `[usercopy-no-cr3-memcpy]` | `load_page_directory` then `memcpy((void *)` / `memset((void *)` in same function under `kernel/syscalls/**` or signals |
| `[usercopy-signals]` | any `memcpy((void *)` in `kernel/lib/signals.c` |
| `[usercopy-sys-uname]` | `memset(buf` / `strncpy(buf->` inside `sys_uname` |

Host contract: `tests/host/test_usercopy_contract.c` (`usercopy_no_raw_user_touch`)
asserts the same signals / `sys_uname` properties without QEMU.

Together with existing `check_devfs_usercopy_contract`, usercopy rules live next
to other **tree-shape** guards in [`DECOUPLING.md`](DECOUPLING.md) — failures
point at a **path + line**, not a vague “session died”.

## Runtime diagnosis (serial)

| Tag | Meaning |
|-----|---------|
| `USER_FAULT_FRAME` | Frame dump before kill/handler (`cr2`, `rip`, `cs`, `err`, pid) |
| `CLASSIFY KERNEL_UACCESS_FAULT` | #PF in kernel (`cs=8`) on a user VA — **contract violation or missing mapping** |
| `CONSOLE_SESSION_SEGV` | Shell/session died (symptom only) |

Soak harness [`scripts/smoke_session_soak.py`](../scripts/smoke_session_soak.py)
treats `KERNEL_UACCESS_FAULT` as **FATAL** (same class as panic / canary).

### Bisect checklist

1. Rebuild userspace ISO after kernel change; verify bin ↔ ISO digest match
   (stale ISO falsely blames unrelated symbols).
2. Grep serial: `KERNEL_UACCESS_FAULT` count must be **0** at login+prompt for
   a healthy session.
3. `addr2line` on `rip` from `USER_FAULT_FRAME`: if it lands in `memcpy` /
   `string.c`, search callers for raw user stores (should be impossible under
   arch-guard for signals/syscalls P0).
4. If `cs` is user ring: not a uaccess kernel bug — treat as userspace / signal
   delivery after a successful copy.

## Gates (verified 2026-09-02)

```bash
make -s kernel-x64.bin
make -s arch-guard
make -s -C tests/host run          # includes usercopy_no_raw_user_touch
make -s ir0                        # rebuild kernel-x64.iso after kernel changes
# then md5 the extracted ISO kernel against kernel-x64.bin
python3 scripts/smoke_session_soak.py --rounds 2   # needs an ISD-built disk
# expect: 0× KERNEL_UACCESS_FAULT, session_segv=0
```

## Related documents

- [`MEMORY.md`](MEMORY.md) — COW / page-fault policy  
- [`PROCESSES.md`](PROCESSES.md) — signals / wait  
- [`DECOUPLING.md`](DECOUPLING.md) — façade + arch-guard map  
- [`devfs-io-contract.md`](devfs-io-contract.md) — device read/write usercopy  
- [`mandocs/en/syscalls.md`](mandocs/en/syscalls.md) — syscall entry overview  
- [`mandocs/en/mm.md`](mandocs/en/mm.md) — MM internals  

## Known limits / not in this oleada

- Optimal asm `get_user` / `put_user` (macros over `copy_*` only).
- Full arm64 production MM + COW parity.
- Exhaustive ban of every possible `memset` into a misnamed local in all of
  `kernel/` — guard is heuristic on the known P0 antipatterns; extend when a
  new caller class appears.
