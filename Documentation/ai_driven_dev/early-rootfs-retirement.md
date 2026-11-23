# Early rootfs retirement (Q1)

Maintainer decision Q1 (`scripts/smp_entry_gate/decisions.tsv`) retires the
"early rootfs world": the single-process syscall emulation under
`kernel/syscalls/{erfs,early_,product_}*`, `fs/{ramtab,bpath,bpivot,bpivot_boot}.c`,
the never-registered `syscall_context_provider` layer and their
`includes/ir0/` headers.

None of the handlers below ran in a linked kernel when they were removed: the
providers were never registered (only `tests/host` did). They are listed so
the logic can be read back when the live subsystem grows the syscall (post-SMP
work). Recover any file with:

    git show 7b98038a:<path>

Most handlers emulate one process with globals (`ep_g_*`, `g_*`); none is
Linux-correct for several tasks. Read them as notes on the expected
arguments and errnos, never as code to restore as is.

## Handlers with logic the live path lacks

The live dispatch (`kernel/entry/syscall_dispatch.c`) returns `-ENOSYS` for
every syscall in this table except `sigaltstack`.

| Syscall | Path:line at 7b98038a (blob) | What it did |
|---|---|---|
| sigaltstack | kernel/syscalls/early_process_helpers.c:197 (14d73ff03920) | Saved and returned one global sp/flags/size triple; no SS_ONSTACK or MINSIGSTKSZ checks. |
| sched_setaffinity | early_process_helpers.c:994 | ESRCH for an unknown pid, EINVAL for len 0 or not a multiple of 8 and for a mask without CPU 0; otherwise accepted and ignored. |
| sched_getaffinity | early_process_helpers.c:1014 | Wrote a one-CPU mask (bit 0), returned 8; EINVAL for len < 8 or unaligned. |
| sched_{get,set}param, {get,set}scheduler, {get,set}attr, rr_get_interval, get_priority_{min,max} | early_process_helpers.c:939-1335 | Global policy and priority; setparam accepted only priority 0 (SCHED_OTHER), ESRCH for an unknown pid. |
| membarrier | early_process_helpers.c:1109 | QUERY returned the supported mask; GLOBAL, GLOBAL_EXPEDITED, PRIVATE_EXPEDITED and the REGISTER commands ran `barrier()`; flags != 0 gave EINVAL. |
| madvise | kernel/syscalls/early_mm.c:301 (2b071bb9cfd6) | Accepted every advice and returned 0 (no-op). |
| msync | early_mm.c:307 | EINVAL for len 0, an unaligned address or unknown flags (MS_ASYNC, MS_INVALIDATE, MS_SYNC); otherwise 0. |
| mseal | early_mm.c:148 | Recorded sealed ranges; mprotect over a sealed range returned EPERM. |
| mincore | early_mm.c:219 | Filled one byte per page from the page-present probe. |
| mlock, mlock2, munlock, mlockall, munlockall | early_mm.c:251 | Range and flag validation only (MLOCK_ONFAULT, MCL_*); no pinning. |
| process_vm_readv / writev | early_process_helpers.c:1031 | Copied iovec to iovec through a 256-byte bounce buffer within the one address space. |
| getcpu | early_process_helpers.c:917 | Wrote cpu 0 and node 0. |
| kcmp | early_process_helpers.c:1336 | ESRCH for a dead pid; KCMP_FILE compared early fd slots; the other types returned 0 (same object). |
| capget / capset | early_process_helpers.c:1170, 1196 | Only `_LINUX_CAPABILITY_VERSION_3` (EINVAL and the version written back otherwise); one global capability set. |
| ioprio_set / ioprio_get | early_process_helpers.c:1225, 1245 | Global ioprio with class and `which` validation. |
| sethostname / setdomainname | early_process_helpers.c:1127 | Length check against the UTS field and copy into globals. |
| times, getrusage | early_process_helpers.c:1146, 139 | times returned ticks at 100 Hz from the counter with a zeroed tms; getrusage wrote a zeroed rusage. |
| pidfd_open / send_signal / getfd | early_process_helpers.c:1350-1391 | Pid validation and errnos; no pidfd object. |
| clone3, futex_waitv, futex_wake/wait (futex2), futex_requeue | early_process_helpers.c:592, 311, 357, 380, 399 | Struct-size and flag checks for the futex2 and clone_args ABI. |
| landlock_create_ruleset / add_rule / restrict_self | kernel/syscalls/erfs_landlock.c:91, 167, 210 (bf0f12aa9f01) | One ruleset of path-beneath rules checked on open. |
| *xattr, xattrat, name_to_handle_at, open_by_handle_at, cachestat, file_{get,set}attr | kernel/syscalls/erfs_xattr.c:78-540 (7dee34da1cab) | In-memory xattr table keyed by node; struct-size checks for the *at variants. |
| AF_UNIX socket family (abstract and path names, socketpair, dgram) | kernel/syscalls/erfs_socket.c (2640254d74b4), ipc/early_unix.c (f42174f980dc) | Fixed slot table of stream and dgram endpoints. |
| pipe, eventfd, memfd, timerfd, epoll slots | ipc/early_anonfd.c (aac79adffe94) | Fixed-size anonymous-file tables. |
| ELF initial stack layout | kernel/exec/early_elf_stack.c (30bc62ca55bd) | argv/envp/auxv builder for the early exec; the common path uses fs/binfmt_elf.c. |

