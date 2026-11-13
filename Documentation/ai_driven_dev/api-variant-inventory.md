# IR0 public API variant inventory and critical-path hygiene

Snapshot of `dev` at `3fac1100` (2026-11-09). Read-only audit; no behavior
changes. Criteria: `rules/ir0-api-public-private.md` and the maintainer's
structure-hygiene rule (sections 1, 3 and 5): one public API per behavior,
the operation validates itself, at most one public predicate, no `#if`, ISA
register names or inline klog/KTM in cross-ISA critical paths.

**Method.** Prototypes were extracted per header; caller counts are
grep-level call sites (`name(`) in `arch block drivers fs includes ipc kernel
ktm mm net sched sound`, excluding the declaration and one definition. Arch
backends that define the same symbol twice (x86-64 + arm64) or KTM mocks can
inflate a count by one; treat small counts as "about". Recount before acting.

**Families being changed by concurrent branches** (do not start work there
until they integrate; re-audit afterwards):

| Branch | Touches | Families marked below |
|--------|---------|-----------------------|
| `refactor/blocking-linux` | `kernel/syscalls/process_syscalls.c`, `syscall_dispatch.c`, sigreturn/restart selftests | Signals, syscall resume (`syscall_resume_rax`) |
| switch series (already on dev at `3fac1100`) | `sched/switch/*` | Context switch |
| `refactor/mm-diag-headers` | `mm/page_fault*.c`, `mm/page_fault_diag.c` -> `mm/diag.c`, `includes/ir0/mm_diag.h`, `kernel/mm_diag.c` | Page-fault logging, mm diag |
| `fix/security-2` | mm COW/mprotect selftests | Page map/protect |
| `fix/arm-parity-2` | selftests, smoke autokill | arm64 backends |
| `refactor/net-fs-split` | net/fs file splits (no diff yet) | socket and fs syscalls |

Verdict legend: **KEEP** (justified), **MERGE INTO x**, **DELETE** (no or
one caller, alias, wrapper), **PRIVATE** (keep, but make it `static` or
subsystem-private).

---

## 1. API families

### 1.1 Signals (send / kill / force) — IN FLIGHT (`refactor/blocking-linux`)

Header `includes/ir0/signals.h` (defs in `kernel/signals.c` unless noted).

| Symbol | Does | Calls | Verdict |
|--------|------|------:|---------|
| `send_signal(pid, sig)` | Post a signal to a pid, wake it | 16 | KEEP: the one send API (Linux `kill_pid`/`send_sig`) |
| `send_signal_pgrp(pgid, sig)` | Post to a process group | 5 | KEEP (Linux `kill_pgrp`) |
| `force_sig_fault(sig, fault, frame)` | Synchronous fault signal; terminates if no handler | 3 | KEEP (Linux name) |
| `signal_deliver_fault(sig, fault, frame)` | Same delivery, returns instead of exiting | 2 | MERGE INTO `force_sig_fault` (make static; its two callers are the fault path) |
| `signal_exit_current(sig)` | Fatal default action, noreturn | 15 | KEEP (single fatal exit; `kernel/process/signals.c`) |
| `handle_signals()` | Run pending signals for current | 22 | KEEP for now; see blocking work |
| `signals_prepare_user_return(p)` | Signal work on the return-to-user path | 2 | KEEP |
| `signals_pause_should_interrupt(p)` | Pending-signal predicate for sleepers | 22 | KEEP; it is the one "signal pending" predicate (Linux `signal_pending`), rename candidate |
| `signals_should_handle_on_run(p)` | Pending work incl. default actions | 4 | MERGE INTO `signals_pause_should_interrupt` (second predicate for the same question) |
| `register_signal_handler(sig, h)` | Install a handler (rt_sigaction) | 3 | PRIVATE (only `sys_rt_sigaction`) |
| `signal_ignore(sig)` | Set SIG_IGN | 0 | DELETE |
| `signals_has_user_handler(p, sig)` | Handler installed? | 8 | KEEP |
| `signals_reset_on_exec(p)` | exec reset | 3 | KEEP |
| `signals_on_sigreturn(p)` | Restore mask after sigreturn | 2 | KEEP |
| `signals_try_abandon_sigframe(p)` | Drop a half-built frame | 1 | PRIVATE / DELETE with blocking rework |
| `signals_deliver_from_irq_frame(p, sig, frame, ...)` | Deliver from an IRQ frame | 2 | KEEP until the IRQ return path is unified |
| `signal_note_syscall_return(p, ret)` | Restart bookkeeping | 1 | PRIVATE (blocking branch) |
| `linux_sigword_to_ir0` / `ir0_sigword_to_linux` / `ir0_sigset_low32` / `ir0_sigset_set_low32` | sigset conversions (inline) | 2-4 | KEEP, one pair is enough: MERGE `*_sigword_*` into the `sigset_*low32` pair |
| `process_signal_default_kill` / `process_signal_is_default_fatal` (`kernel/process.h`) | Fatal default action for kill(2) | 3 / 3 | KEEP private to process/ (not in a public header) |
| `oom_kill_process` (`includes/ir0/oom.h`) | OOM victim kill | 4 | KEEP (separate policy) |

Hand-posted signals that bypass `send_signal` (one API rule):

| Site | Code | Fix |
|------|------|-----|
| `kernel/syscalls/fs_rw.c:253`, `:264` | `signal_pending |= SIGNAL_MASK(SIGPIPE)` twice, copy of the SIG_IGN check | `send_signal(current pid, SIGPIPE)` (Linux `send_sig(SIGPIPE, current, 0)`) |
| `kernel/syscalls/process_syscalls.c:1958` | `signal_pending |= SIGNAL_MASK(SIGSEGV)` on bad sigreturn frame | Linux `force_sigsegv()`; `kernel/signals.c:680` already has a private `signal_force_segv` that should become the one helper (blocking branch is rewriting this exact code) |
| `kernel/signals.c:299`, `:439`, `:680` | Internal posting | fine (owner) |

`includes/ir0/signal_irq.h` (13 arch-implemented hooks, x86-64 and arm64
backends): KEEP as the ISA hook set, but its names mix layers
(`signal_fill_sigcontext_from_syscall_frame`, `signal_redirect_irq_frame`,
`irq_frame_sp`). `signal_sigcontext_validate_and_sanitize` reads the user
`sigcontext` in place (see 1.2, bug B2).

`includes/ir0/signal_syscall_resume.h`: 9 inline helpers that patch syscall
frames to "resume" blocked syscalls (`signal_resume_blocked_syscall_frame`,
`signal_repair_sigcontext_read_fd`, `signal_syscall_*_suspicious`). This is
the "file that exists only to patch a crooked design" case of rule 5: DELETE
the header once `refactor/blocking-linux` lands (sleep on the kernel stack and
restart via `-ERESTARTSYS`). `signal_syscall_arg_suspicious` already has 0
callers.

`includes/ir0/fault.h`: `fault_signal` (3) KEEP; `ir0_fault_from_page_fault`
(2) KEEP; `ir0_fault_kind_name` (4, one file) PRIVATE to the diag file.

### 1.2 User copy and validation

Header `includes/ir0/copy_user.h` (`mm/copy_user.c`, `mm/paging_uaccess.c`).

| Symbol | Calls | Verdict |
|--------|------:|---------|
| `copy_to_user` | 232 | KEEP |
| `copy_from_user` | 201 | KEEP |
| `strncpy_from_user` | 11 | KEEP |
| `clear_user` | 1 | KEEP (Linux name, cheap) |
| `copy_to_user_mm` / `copy_from_user_mm` / `clear_user_mm` | 28 / 14 / 5 | KEEP (remote-mm variant, Linux `access_remote_vm` role) |
| `put_user` / `get_user` (macros) | 0 / 0 | DELETE or start using; today they are dead aliases |
| `access_ok` | 97 | KEEP as the single public predicate, but see the call-site classes below |
| `mm_user_va_ok` (`includes/ir0/mm.h`, arch-defined) | 34 | DONE (`refactor/uaccess-single-check`): mm-private in `mm/user_va.h`, guarded. Was MERGE: second public predicate for the same contract (`access_ok` is `mm_user_va_ok` plus the kernel-thread pass). Make it mm-internal (`mm/` + arch backends) and route the remaining callers through `access_ok` or a semantic helper |
| `mm_user_range_ok` (`includes/ir0/mm_range.h`, inline) | 2 (both arch `mm.c`) | PRIVATE: it is the arithmetic behind `mm_user_va_ok`; move to an arch-shared private header |
| `copy_path_from_user`, `ir0_resolve_user_path[_at]`, `ir0_resolve_kpath_at` (`path_user.h`) | 5 / 25 / 7 | KEEP `copy_path_from_user` (Linux `getname`) + one resolver; `ir0_resolve_user_path` vs `_at` is a MERGE (AT_FDCWD parameter) |
| `ir0_stat_to_user` / `ir0_copy_stat_to_user` (`stat_user.h`) | 1 / 4 | MERGE into one `cp_new_stat`-style function |

`access_ok` call sites (94 outside `mm/copy_user.c`), by class:

| Class | Count | Files (count) | Action |
|-------|------:|---------------|--------|
| A. Pre-validation immediately followed by a copy of the same pointer (redundant: the copy returns `-EFAULT`) | ~69 | `process_syscalls.c` (26 total), `fs_path_syscalls.c` (25), `fs_meta.c` (9), `io_syscalls.c` (7), `socket_syscalls.c` (6), `time_syscalls.c` (5), `fs_syscalls.c` (4), `epoll_syscalls.c` (4), `fs_rw.c` (2), `fork.c` (2), `wait.c`, `exit_bind.c` | DELETE the check; keep the copy's error |
| A'. `access_ok(path, 256)` before path resolution (fixed 256, not the real length) | 10 | `fs_meta.c:26,102,279,331,333`, `fs_syscalls.c:846,961`, `fs_path_syscalls.c:328,539` | DELETE; `strncpy_from_user` inside the resolver already validates |
| A''. Out-buffer checked up front, copy happens later (stat buf, getdents, pipe fds, epoll events, execve argv/envp first slot) | ~9 | `fs_meta.c:132,215,245`, `fs_dirent.c:274`, `io_syscalls.c:1849`, `epoll_syscalls.c:332`, `process_syscalls.c:1251,1253`, `procfs_process.c:1024` | DELETE once the later copy's error is propagated (check that each one is) |
| B. Guard for a raw user dereference (BUG: no copy at all) | 3 | `process_syscalls.c:1665` (`oldact->sa_handler = ...` writes user memory), `:1678` (`act->sa_handler` read), `:1940` (`restore_ctx = ctx` then `signal_sigcontext_validate_and_sanitize(ctx)` reads the user frame) | Fix red/green: `copy_from_user` into a kernel `struct sigaction` / `struct sigcontext`; `copy_to_user` for `oldact`. 1940 is inside `refactor/blocking-linux`'s sigreturn rework — coordinate |
| C. Consume-before-copy guard (state consumed, then copied) | 3 | `socket_syscalls.c:569,581` (datagram recvfrom), `:1303` (recvmsg iov) | Restructure Linux-style (copy first, consume what reached userspace); `refactor/net-fs-split` owns these files |
| D. True no-copy (pointer stored, not touched now) | 1 | `process_syscalls.c:1764` (`set_tid_address`) | KEEP (Linux does not even check; acceptable) |

`mm_user_va_ok` call sites (34), by class:

| Class | Sites | Action |
|-------|-------|--------|
| mm internals (the copy itself, fault classification) | `mm/paging_uaccess.c:117,156,192`, `mm/copy_user.c:35`, `mm/page_fault_user.c:194`, `mm/page_fault_kernel.c:28` | KEEP, mm-private |
| "Is this IP/SP a user address" for task state | `sched/switch/kernel_resume.c:47,64,68,87,88`, `kernel/process/core.c:33,341,369`, `kernel/process/wait_audit.c:20,22` | MERGE into one semantic predicate (`task_ip_is_user()`-style, owned by process/arch) — these are not syscall pointers |
| Handler / restorer / sigcontext IP checks (no copy, Linux `access_ok`) | `kernel/signals.c:169,603,849,977,988,1188` | use `access_ok` |
| mmap/munmap/mprotect/brk/futex/prctl ranges | `kernel/syscalls/mm_syscalls.c:158,594,723,798,850,1003,1059`, `mm_brk.c:88`, `process_syscalls.c:704,1734` | use `access_ok` (Linux checks `TASK_SIZE`/`access_ok` here) |

### 1.3 Strings and memory helpers

Header `includes/string.h` (`includes/string.c`).

| Symbol | Calls | Verdict |
|--------|------:|---------|
| `strlen strcmp strncmp strcpy strncpy strchr strrchr strstr memset memcpy memmove memcmp snprintf vsnprintf` | 6-492 | KEEP (libc names) |
| `kstrlen` / `kstrcmp` / `kstrncmp` | 7 / 13 / 2 | MERGE INTO `strlen` / `strcmp` / `strncmp` (duplicates) |
| `kmemset` / `kmemcpy` / `kstrncpy` (macros over `__k*_checked` and `__k*_impl`) | 22 / 25 / 10 | MERGE INTO `memset` / `memcpy` / `strncpy` (a parallel "checked" family for the same behavior; if the bounds diagnostics are wanted, put them inside the one function under a debug config) |
| `__kmemset_impl __kmemcpy_impl __kstrncpy_impl __k*_checked` | 1 each | DELETE with the above |
| `strcat`, `sprintf`, `strtok`, `atol`, `isalnum`, `isspace`, `toupper`, `tolower` | 0 | DELETE (dead) |
| `strncat`, `strtok_r`, `strtoul`, `strtol`, `isdigit`, `isalpha`, `isupper`, `islower` | 1-2 | KEEP (tiny libc surface) or DELETE where the single caller can use another |
| `itoa` | 16 (2 files) | MERGE INTO `snprintf` |
| `atoi` | 12 | KEEP |

Private duplicates of public helpers: `drivers/timer/hpet/find_hpet.c:18`
static `memcmp`, `drivers/timer/acpi/acpi_pm.c:82` `acpi_memcmp`,
`kernel/syscalls/socket_syscalls.c:83` `sock_strnlen` (add one public
`strnlen`), `kernel/elf_loader.c:297` `exec_strdup_kern` (add `kstrdup` only
if a second user appears). The ARM64 freestanding string copies
(`portable_string.c`, `freestanding_stubs.c`) left with the board `-min`
images.

### 1.4 Locks and IRQ save/restore

| Symbol | Header | Calls | Verdict |
|--------|--------|------:|---------|
| `irq_save` / `irq_restore` | `ir0/cpu.h` (arch `mm_ops.c`) | 74 / 111 | KEEP (Linux `local_irq_save`); the restore surplus is multiple-return paths |
| `enable_interrupts` / `disable_interrupts` | `ir0/cpu.h` | 14 / 7 | KEEP |
| `ir0_spin_lock` / `ir0_spin_unlock` (`ir0_spinlock_t`) | `ir0/spinlock.h` | 29 / 41 | KEEP as the lock API; rename to `spin_lock_irqsave`-style later |
| `rq_lock` / `rq_unlock` | `sched/core.h` | 13 / 21 | KEEP (documented SMP seam) |
| `process_registry_lock` / `unlock` | `kernel/process/process_internal.h` | 14 / 18 | KEEP (named lock with a real owner) |
| Per-file `X_irq_save` / `X_irq_restore` one-line wrappers around `irq_save`: `tcp` (25/49), `arp` (9/17), `sock` (`sock_udp.c`, 9/13), `icmp` (7/12), `sock_icmp` (9/11), `dns` (9/10), `dhcp` (7/8), `udp` (4/7), `named_fifo` (5/11), `named_devnode` (6/11), `process_irq_*` (11/19), `tty` (3/3), `rtl8139` (3/4), `pmm_lock` (11/17) | private | ~280 sites | MERGE INTO `ir0_spin_lock` on a per-subsystem `ir0_spinlock_t` (that gives SMP a real lock object) or plain `irq_save`. `named_fifo.c` / `named_devnode.c` also carry a `#ifdef TEST_HOST` copy of the wrapper: use the host stub of `irq_save` instead |
| `swap_lock` (`mm/swap/swap.c`), `reclaim_lock` (`mm/reclaim.c`) | private test-and-set spinners | 10 / 3 | MERGE INTO `ir0_spin_lock` (two more hand-rolled lock types) |
| `ipc_lock` / `ipc_unlock` (`ipc/ipc.c:510`) | `#if 0` dead code | 0 | DELETE |

### 1.5 Time and clock getters

Header `includes/ir0/clock.h` (`drivers/timer/clock_system.c`).

| Symbol | Calls | Verdict |
|--------|------:|---------|
| `clock_get_uptime_milliseconds` | 88 | KEEP as the monotonic getter (rename `ktime_get_ms`-style later) |
| `clock_get_tick_count` | 8 | KEEP (scheduler tick) |
| `clock_get_current_time` | 14 | KEEP (realtime) |
| `clock_get_uptime_seconds`, `get_system_time` | 0 | DELETE (derivable aliases) |
| `init_clock`, `detect_best_clock`, `get_current_timer_type`, `clock_set_current_time`, `clock_set_timezone_offset`, `clock_get_timezone_offset`, `clock_get_active_timer`, `clock_set_alarm`, `clock_cancel_all_alarms`, `clock_set_scheduler_quantum`, `clock_get_scheduler_quantum`, `clock_sched_resched_pending_peek`, `clock_print_stats` | 0 | DELETE (13 dead public entry points) |
| `clock_take_sched_resched_pending` / `clock_request_sched_resched` | 1 / 3 | KEEP (one pair) |
| `clock_sleep` / `clock_sleep_ticks` | 1 / 1 | MERGE (one sleep, unit as parameter) or move to `clock_wait.h` |
| `timer_read` (`ir0/cpu.h`, arch) | 4 | KEEP (raw arch counter) |
| `early_clock_read` / `early_clock_available` / `early_clock_quality` (`early_clock.h`) | 1 / 3 / 0 | KEEP the first two (pre-timer klog stamps); DELETE `early_clock_quality` |
| `early_time_smoke_ok` (`early_time.h`) | 0 | DELETE |
| `get_pit_ticks` (`drivers/timer/pit/pit.h`) | only in `pit.c` | PRIVATE |
| `rtc_write_register`, `rtc_get_time_string`, `rtc_get_date_string` (`rtc.h`) | 0 | DELETE |
| `delay_ms` (`ir0/vga.h`!) | 0 | DELETE (and it never belonged in vga.h) |
| `ir0_clock_wait_service_clockevents` (`clock_wait.h`) | 0 | DONE (deleted with the clock-wait idle step) |
| `minix_now_seconds` (13), `pseudo_fs_stat_now` (16), `clock_now_ticks` (erfs, 5) | private | fs-local "now" wrappers; MERGE INTO `clock_get_current_time` |

### 1.6 Process and fd lookup

| Symbol | Header | Calls | Verdict |
|--------|--------|------:|---------|
| `current_process` (global) | `kernel/process.h` | 1176 | KEEP (Linux `current`) |
| `process_get_current()` | `kernel/process.h` | 27 | MERGE INTO `current_process` (accessor alias) |
| `process_get_pid()` / `process_get_ppid()` | `kernel/process.h` | 6 / 2 | DELETE (aliases of `current_process->task.pid` / `->ppid`) |
| `process_find_by_pid` | `kernel/process.h` | 56 | KEEP (Linux `find_task_by_vpid`) |
| `process_find_child_of` | `kernel/process.h` | 2 | KEEP (wait) or PRIVATE |
| `get_process_list` | `kernel/process.h` | 2 | PRIVATE (registry iteration should be one `for_each_process`) |
| `get_process_fd_table()` (def in `io_syscalls.c`) | `ir0/files_struct.h` | 48 | MERGE INTO `ir0_fd_get`: raw table pointer plus 38 open-coded `fd < 0 || fd >= MAX_FDS_PER_PROCESS || !in_use` checks in 17 files (`socket_syscalls.c` 6, `fs_meta.c` 5, `fs_path_syscalls.c` 4, ...) |
| `ir0_fd_get` / `ir0_fd_put` | `ir0/fd_get.h` | 8 / 11 | KEEP (Linux `fdget`/`fdput`); it is the one validated lookup |
| `ir0_fd_install_product` | `ir0/fd_install.h` | 0 | DELETE |
| `fd_entry_is_redirected` (inline) | `ir0/fd_get.h` | 7 (1 file) | PRIVATE to its user |
| `fd_slot_note_created/destroyed`, `fd_slot_stats_get` | `ir0/files_struct.h` | 14 / 4 / 1 | KEEP (accounting); move the defs out of `io_syscalls.c` |
| erfs fd helpers (`close_fd`, `fd_is_open`, `flags_of_fd`, `slot_from_fd`, ... in `kernel/syscalls/erfs_internal.h`) and `early_rootfs_fd_is_*` (`ir0/early_rootfs.h`, 9 predicates) | — | — | Separate early-rootfs fd world; KEEP private but do not grow; one `early_rootfs_fd_kind(fd)` could replace the 9 `_is_*` predicates |

### 1.7 Page map / unmap / protect and mm activation — partly IN FLIGHT (`fix/security-2`, `refactor/mm-diag-headers`)

`includes/ir0/paging.h` is only `#include <mm/paging.h>`, so the whole
`mm/paging.h` is a public facade by accident.

| Symbol | Calls | Verdict |
|--------|------:|---------|
| `map_page_in_directory(root, va, pa, flags)` | 26 | KEEP as the one map primitive |
| `map_page(va, pa, flags)` | 5 | MERGE INTO `map_page_in_directory(current root, ...)` |
| `map_user_page` | 1 | MERGE / DELETE |
| `map_user_region` | 0 | DELETE |
| `map_user_region_in_directory` | 12 | KEEP (range form) |
| `unmap_page_in_directory` | 29 | KEEP |
| `unmap_page` | 0 | DELETE |
| `protect_user_region_in_directory` | 4 | KEEP |
| `is_page_mapped_in_directory` / `paging_get_pte` | 30 / 21 | Two lookups for one walk: KEEP `paging_get_pte`, MERGE the predicate as an inline over it |
| `paging_install_pte`, `paging_ensure_4k_leaf` | 1 / 3 | PRIVATE to mm |
| `map_supervisor_identity_low` / `_2mb` / `map_supervisor_device_2mb` | 3 / 3 / 1 | KEEP (boot) |
| `paging_activate_address_space(root)` | 12 | KEEP as the one activation API (Linux `switch_mm`) |
| `mm_activate(root)` (`ir0/tlb.h`, arch) | 6 | PRIVATE: arch hook behind the above; three arm64 callers and one x86 caller bypass the `mm_prepare_root_activation` step (`arch/arm64/sources/switch_early.c:19`, `first_switch.c:41`, `boot_common.c:202`, `arch/x86-64/sources/mm_ops.c:76`) — review that each bypass is intentional |
| `paging_current_address_space()` vs `mm_current_root()` | 17 / 5 | MERGE: the first is a pure alias of the second |
| `mm_prepare_root_activation` | 2 | PRIVATE (activation step) |
| `enable_paging` / `setup_and_enable_paging` / `is_paging_enabled` | 1 / 0 / 1 | DELETE `setup_and_enable_paging`; arch already has `mm_enable_translation` / `mm_translation_enabled` / `mm_translation_ready` (3 more names for the same state) |
| `paging_ir0_mm_category_stats`, `paging_steady_state_audit`, `paging_oom_audit` | 0 | DELETE (or move to the mm diag file) |
| `pmm_alloc_frame` outside mm (`mm_syscalls.c`, `ipc/sysv_shm.c`, `arch/x86-64/sources/task_stack.c`) | — | use `mm_alloc_frame` (see 1.11) |
| `drivers/timer/acpi/acpi_pm.c:125-147` | reads `mm_current_root()` into `cr3`, masks it as a `pml4` pointer | use `map_page_in_directory(paging_current_address_space(), ...)`; no x86 root decoding in a driver |

### 1.8 Logging

| Layer | Symbols | Calls | Verdict |
|-------|---------|------:|---------|
| Core (`ktm/include/klog.h` via `ir0/klog.h` -> `ir0/ktm/klog.h`) | `klog_{trace,debug,info,notice,warn,error,fatal}` and `_fmt` variants | 2-177 each | KEEP one set. Recommend `_fmt` only (Linux `pr_*` are all printf); the non-fmt set (146 calls) is MERGE INTO `_fmt` |
| `klog_print`, `klog_hex32`, `klog_hex64`, `klog_smoke` | 289 / 23 / 32 / 111 | KEEP `klog_print` (raw, no level) for early/panic; MERGE hex helpers into `_fmt` |
| `kprintf` / `kvprintf` / `kprintf_level` | 8 / 1 / 0 | MERGE INTO `klog_info_fmt` (Linux `printk`); DELETE `kprintf_level` |
| `klog_emit`, `klog_trace` | 9 (1 file) / 2 | PRIVATE |
| `klog_get_profile`, `klog_record_count`, `klog_get_boot_phase` | 0 | DELETE |
| Alias layer 1 (`ir0/logging.h`) | `log_{debug,info,warn,error,fatal}[_fmt]`, `log_subsystem_ok` | ~78 | DELETE (pure `#define` aliases of `klog_*`; keep `log_subsystem_ok` as a function if wanted) |
| Alias layer 2 (`ir0/logging.h`) | `LOG_{DEBUG,INFO,WARNING,ERROR}[_FMT]` | 334 | DELETE (aliases of aliases); `LOG_FATAL[_FMT]` have 0 callers |
| Wrapper header | `kernel/log.h` (only `#include <ir0/logging.h>`) | 2 includers | DELETE |
| Boot log (`ir0/boot_log.h`, `arch/common/boot_log.c`) | `ir0_boot_{info,notice,warn,info_hex64,arch,smoke}` | 1-72 | KEEP (pre-klog serial channel), but `ir0_boot_smoke` (72) vs `klog_smoke` (111) are two smoke-tag emitters: MERGE |
| Serial (`ir0/serial_io.h`) | `serial_print`, `serial_print_hex32/64`, `serial_putchar` | 28 / 3 / 2 / 12 | KEEP as the driver; callers outside `ktm/klog.c`, panic and boot should log via klog (allowlist exists: `check_klog_serial_print_allowlist`) |
| Screen print (`ir0/vga.h`) | `print` (inline, 48), `print_colored`, `print_error/warning/success`, `print_hex*`, `print_uint*`, `print_int32`, `print_screen_only` | 0-48 | Console output, not logging: see 1.10. `print_hex32`, `print_hex8`, `print_hex`, `set_cursor_pos` have 0 callers: DELETE |

### 1.9 Driver and device registration

| Symbol | Header | Calls | Verdict |
|--------|--------|------:|---------|
| `ir0_register_driver` | `ir0/driver.h` | 11 | KEEP |
| `ir0_unregister_driver`, `ir0_find_driver`, `ir0_driver_get_state`, `ir0_driver_shutdown_all`, `ir0_register_simple_driver` (inline) | `ir0/driver.h` | 0 | DELETE (or keep `unregister` if hot-unplug is planned) |
| `ir0_driver_registry_is_initialized` | `ir0/driver.h` | 1 | PRIVATE |
| `driver_bootstrap_register` | `ir0/driver_bootstrap.h` | 11 (1 file) | KEEP (boot ordering) but PRIVATE to `drivers/init_drv.c` |
| `init_all_drivers` / `drivers_init` | `ir0/init_drv.h` | 1 / 0 | DELETE `drivers_init` ("compat entrypoint", no callers) |
| `ir0_bluetooth_register_driver` | `ir0/bluetooth.h` | — | MERGE into `ir0_register_driver` with a bus type, or justify |
| `register_multilang_example_drivers` | `drivers/multilang_drivers.h` | — | DELETE if example-only |
| `ir0_block_register` / `ir0_block_lookup_by_name` / `ir0_block_get_info` | `ir0/blockdev.h` | 8 / 13 / 6 | KEEP |
| `block_dev_*` (`ir0/block_dev.h`, 7 functions keyed by name) | `ir0/block_dev.h` | `get_sector_count` 0 | Two block facades (`block_dev.h` includes `blockdev.h`): MERGE `block_dev_*` into `ir0_block_*`; `ir0_block_unregister_for_test` 0 callers |
| `devfs_register_node` | `ir0/devfs.h` | 5 | KEEP |
| `devfs_register_device` / `devfs_unregister_device` | `ir0/devfs.h` | 0 | DELETE |
| `devfs_find_node` / `_by_id` / `_by_rdev` | `ir0/devfs.h` | 3 / 5 / 2 | three lookups: KEEP `_by_rdev` (Linux key) and path lookup; `_by_id` is already fenced by a guard |
| `ensure_devfs_init` (def in `io_syscalls.c`) | `ir0/devfs.h` | 11 | DELETE: lazy-init guard sprinkled over callers; init devfs once at boot |
| `resource_register_*` / `resource_unregister_*` / `resource_foreach_*` | `ir0/resource_registry.h` | 3-10 | KEEP |
| fs registration (`vfs_register_fs` + `minix_fs_register`, `tmpfs_register`, `fat16_fs_register`, `ext2_fs_register`, `hostshare_9p_register`, `simplefs_register` + `simplefs_engine_register`) | fs-private headers | — | KEEP `vfs_register_fs`; the per-fs `*_register` wrappers are fine if each only calls it; `simplefs` has two |
| `net_register_device` / `net_register_protocol` | `ir0/net.h` | — | KEEP |

### 1.10 Console and tty output

| Symbol group | Header | Verdict |
|--------------|--------|---------|
| tty core: `tty_read_kernel`, `tty_write_kernel`, `tty_ioctl_termios_kernel`, `tty_flush_input`, `tty_input_char`, `tty_input_bytes_available` | `ir0/console.h` | KEEP |
| `ir0_console_*` (35 functions in the same header) | `ir0/console.h` | Overlaps the tty set: `ir0_console_flush_input` vs `ir0_console_flush_input_session` vs `tty_flush_input`; `ir0_console_read` / `ir0_console_write` vs `tty_read_kernel` / `tty_write_kernel`. DELETE the 0-caller ones: `ir0_console_resched_pending`, `ir0_console_in_tty_sleep`, `ir0_console_timer_resched_pending`, `ir0_console_drain_echo`, `ir0_console_isatty`, `ir0_console_fill_termios`, `ir0_console_set_termios`. `ir0_console_after_tty_read_signal` / `ir0_console_after_signal_abandon` exist only for the blocked-syscall frame patching: DELETE with the blocking rework |
| `console_backend_*` | `ir0/console_backend.h` | KEEP as the backend contract; 0 callers: `console_backend_fb_scale`, `show_cursor`, `cursor_x`, `set_tty_serial_mirror`, `typewriter_init` |
| `console_*` (video console cells) | `ir0/video_console.h` | KEEP (driver-private set) |
| `typewriter_*` | `ir0/typewriter.h` | 0 callers: `typewriter_get_mode`, `typewriter_print`, `typewriter_print_uint32`, `typewriter_enable_for_commands`, `typewriter_is_enabled_for_commands` -> DELETE |
| `print`, `putchar`, `print_colored`, `print_screen_only`, `print_error/warning/success`, `print_hex*`, `print_uint*`, `print_int32`, `console_puts` | `ir0/vga.h` | One screen-output path is enough: KEEP `console_backend_write` (or `print`) and MERGE the rest. `ir0/vga.h:19` also defines `LOG_OK` / `LOG_WARN` / `LOG_ERR` screen macros, a third `LOG_*` family next to the logging aliases |

### 1.11 Allocation

| Symbol | Header | Calls | Verdict |
|--------|--------|------:|---------|
| `kmalloc`, `kcalloc`, `kfree` | `ir0/kmem.h` | 140 / 16 / 321 | KEEP |
| `kmalloc_aligned` / `kfree_aligned` | `ir0/kmem.h` | 15 / 6 | KEEP for now (DMA/stack users), long-term MERGE (Linux kmalloc is naturally aligned for powers of two, and `kfree` frees both) |
| `__kfree_checked` / `__kfree_aligned_checked` | `ir0/kmem.h` | macro targets | KEEP (implementation of `kfree`) |
| `heap_init` | `ir0/kmem.h` | 6 | KEEP (boot) |
| `heap_alloc` / `heap_free` (`mm/allocator.h`) | mm-private | 3 / 5 | KEEP private (guarded by `[kmem-single-api]`) |
| `dma_alloc` / `dma_free` / `dma_sync_for_*` / `dma_map_mmio` | `ir0/dma.h` | 2 / 2 / 3 / 1 | KEEP (Linux names, both arches) |
| `mm_alloc_frame(ctx)` | `ir0/mm_alloc.h` | 9 | KEEP as the one frame allocator (adds direct reclaim) |
| `pmm_alloc_frame` | `mm/pmm.h` | 12 | PRIVATE to mm: non-mm users (`kernel/syscalls/mm_syscalls.c`, `ipc/sysv_shm.c`, `arch/x86-64/sources/task_stack.c`) skip reclaim; `mm/pmm.h` is also included by `kernel/elf_loader.c` and `kernel/boot_init.c` |
| `pmm_free_frame` vs `pmm_frame_put` | `mm/pmm.h` | 17 / 10 | MERGE: `pmm_free_frame` is literally `pmm_frame_put` |
| `pmm_init` | `mm/pmm.h` | 0 | DELETE (`pmm_init_ranges` is used) |
| `page_table_frame_alloc` / `_free` | `ir0/page_table_pool.h` | 4 / 6 | KEEP (dedicated pool) |

---

## 2. `#if` / `#ifdef` inside critical cross-ISA paths

No ISA conditionals remain in the portable critical paths (`ARCH_*`,
`__x86_64__`, `__aarch64__` occur only in `includes/stdint.h`,
`includes/stddef.h` and a KTM arm64 mock). What is left is config and debug
conditionals:

| File:line | Macro | Guards | Suggested replacement |
|-----------|-------|--------|-----------------------|
| `kernel/syscalls/syscall_dispatch.c:91`, `:116`, `:454` | `CONFIG_ENABLE_NETWORKING` | socket syscall wrappers and table slots | Build `socket_syscalls.c` only with networking and give `syscall_dispatch` a `net_syscalls_register(table)` hook (stub returns nothing when disabled); coordinate with `refactor/net-fs-split` |
| `kernel/signals.c:255` | `SIGNAL_DELIVER_LOG` | delivery klog with `cr2=`/`rip=`/`rsp=` tags | `signal_trace_deliver()` in a `kernel/signal_diag.c` |
| `kernel/signals.c:266` | `CONFIG_KTM_FLIGHT` | two `KTM_FLIGHT` records | same hook; KTM flight wrapper compiles to nothing when disabled |
| `kernel/signals.c:403`, `:449`, `kernel/process/signals.c:82`, `kernel/process/exit.c:60`, `kernel/process/wait.c:185`, `kernel/syscalls/process_syscalls.c:1626` | `IR0_DEBUG_PROC` | `[SIGTERM_AUDIT]` klog | `signal_trace_*()` / `process_trace_*()` hooks in a diag file (stubbed when the debug config is off) |
| `kernel/signals.c:456`, `:716`, `:725`, `:776`, `:851`, `:1124`, `kernel/process/wait.c:67` | `DEBUG_PROCESS` | narrative klog lines | delete (noise) or the same hooks |
| `kernel/process/core.c:136`, `:144` | `CONFIG_DEBUG_ISRABI` | `[ISRABI][IRQ_SAVE]` klog on the IRQ save path | `process_trace_irq_save()` hook |
| `kernel/process/exit_bind.c:17`, `:85` | `CONFIG_ENABLE_NETWORKING` | socket cleanup on exit | `net_files_exit()` stub header |
| `mm/tlb_sync.c:8`, `kernel/process/registry_lock.c:6` | `CONFIG_SMP` | `#error` fences | KEEP (compile-time refusal, not a branch) |
| `mm/page_fault_diag.c:24` | `DEBUG_PAGE_FAULTS` | inside the diag file | KEEP (already the separate diag file; being moved by `refactor/mm-diag-headers`) |
| `kernel/process/wait_audit.c:8,32,48,62` | `IR0_DEBUG_WAIT` | inside the wait audit file | KEEP (separate diag file) |
| `includes/ir0/signals.h:85` | `SIGNAL_DELIVER_LOG` default | | DELETE with the signals.c item |

Clean (no conditionals): `sched/core.c`, `sched/core.h`, `sched/rr.c`,
`sched/prio.c`, `sched/switch/*`, `kernel/fault.c`, `kernel/sigtramp.c`,
`kernel/process/signal_enter.c`, `saved_context.c`, `mm.c`, `mm_struct.c`,
`idle.c`, `kernel/irq_dispatch.c`, `mm/page_fault.c`, `page_fault_user.c`,
`page_fault_kernel.c`, `page_fault_cow.c`, `mm/paging.c`, `mm/copy_user.c`,
`mm/paging_uaccess.c`, `kernel/time/clock_wait.c`.

---

## 3. ISA register names and ISA concepts outside `arch/`

Guarded since this branch: `scripts/architecture_guard.py`
`[portable-no-register-names]` with the ledger
`scripts/isa_register_name_baseline.txt` (88 `<path> <identifier>` pairs:
31 in core trees, 57 in `ktm/`). The ledger only shrinks: arch-guard fails on
a new pair and on a listed pair whose identifier is gone, so a branch that
fixes an offender deletes its line in the same commit. ISA-specific headers
(`arch_syscall_frame_*.h`, `arch_task_context_*.h`, `sigcontext_*.h`,
`syscall_{x86_64,arm64}.h`, `asm_offsets.h`) are exempt. `drivers/` is not
scanned (hardware register vocabulary, and HCI "rsp" = response).

| Location | Identifier / concept | Neutral replacement |
|----------|----------------------|---------------------|
| `kernel/process.h:264` + 10 files (`core.c`, `signal_enter.c`, `wait.c`, `signals.c`, `process_syscalls.c`, `syscall_dispatch.c`, `sched/switch/kernel_resume.c`, arch `context.c` x2, `kernel/test/test_ktm_sched.c`) | `syscall_resume_rax` | **Gone** (blocking rework); ledger lines removed |
| `includes/ir0/signal_syscall_resume.h:91-111`, `kernel/syscalls/process_syscalls.c:1982-2026` | `resume_rax` | **Gone** (blocking rework); ledger lines removed |
| `includes/ir0/syscall_frame.h:37`, `:43`; `kernel/process.h:565`, `:781`; `kernel/process/core.c:186-232` | `rip_hw`, `rax` params (`stack_r9_slot` is gone) (`process_capture_syscall_frame_at_entry`, `process_syscall_restore_exit_regs`) | `entry_ip`, `exit_frame`; the r9-slot contract belongs in `arch/x86-64` |
| `includes/ir0/task_ops.h:34` | `rax` param of `task_apply_syscall_frame` | `retval` |
| `kernel/process.h:591-594` | `fork_restore_emit_pre_iretq`, `fork_ret_first_syscall_entry(rax_hw, rip_hw, rsp_hw)`, `fork_flow_note_kernel_entry(rip_hw, ...)` | `fork_trace_before_user_return`, `(retval, ip, sp)`; these are arch hook prototypes and could move to an arch hook header |
| `includes/ir0/arch_task.h:22-25` (+ `IR0_TASK_{GET,SET}_{CS,SS}`, `task_cs_is_user`) | x86 segment accessors in the portable task facade | move to an x86-private header; users are `arch/x86-64/*` and `ktm/ktm_ctx_snapshot.c`; `task_cs_is_user` -> `task_is_user_mode` |
| `includes/ir0/fault.h:47`, `includes/ir0/page_fault.h:28`, `kernel/fault.c:35`, `mm/page_fault_diag.c:31-77` | `code_seg` field ("x86 CS"), `fault_cs/fault_rip/fault_rsp` | drop `code_seg` from the neutral fault (arch `show_regs` hook prints it); `fault_ip/fault_sp` (mm diag branch is moving this file) |
| `kernel/process.h:116` | `fs_base` union member (x86 TLS register) | `arch_thread.tls_base` only (the comment already says so) |
| `includes/ir0/oops.h:47`, `kernel/oops.c:96` | `ir0_panic_fault_rsp()` | `panic_fault_sp()` |
| `includes/ir0/signal_irq.h:59` | `new_rsp` param | `new_sp` |
| `includes/ir0/ktm/stack_watch.h:20`, `ktm/stack_watch.c` | `ktm_stack_watch(uint64_t rsp)` | `sp` |
| `kernel/signals.c:58-70`, `:211`, `:999`, `:931-959` | `signal_rip_is_registered_handler(p, rip)`, `snap_rdi`, `snap_rsi` | `signal_ip_is_registered_handler(ip)`, `snap_arg0/1` |
| `kernel/elf_loader.c:1513-2042` | `exec_commit_ctx.{task_cr3_entry, active_cr3_entry, entry_rip, task_rip_final, task_rsp_final}` | `{task_root, active_root, entry_ip, final_ip, final_sp}` |
| `kernel/syscalls/fs_path_syscalls.c:154-162` | local `rflags` (means remount flags) | `remount_flags` (misleading name only) |
| `drivers/timer/acpi/acpi_pm.c:125-147` | `cr3`, `pml4`, `& ~0xFFF` decode of the root | `map_page_in_directory(paging_current_address_space(), ...)` |
| `ktm/d1_12_read_diag.c` (26), `ktm/d1_13_malloc_pf_diag.c` (7), `ktm/ktm_panic_class.c` (6), `ktm/include/d1_1{2,3}_*.h` (10), `ktm_probe_diag.[ch]` (4), `ktm/include/ktm.h` (2) | `pre_rax..pre_r10`, `magic_rax`, `USER_RIP_LO/HI`, ... | diag snapshots of x86 syscall registers: read them through `syscall_frame_arg(i)` / `task_get_ip/sp`, or move the x86-only probes under `arch/x86-64` |

ISA concepts that are not register names: the `signal_syscall_resume.h`
frame patching (x86 "blocked syscall frame" model) and
`signals_deliver_from_irq_frame(..., uint64_t *frame, ...)` (raw ISA frame as
`uint64_t *` in a portable signature).

---

## 4. Inline klog / KTM recording in critical source

| File:line | Recording | Subsystem hook it should go through |
|-----------|-----------|-------------------------------------|
| `kernel/signals.c:132,256,404,450,457,717,726,763,768,777,852,875,979,990,1002,1018,1036,1044,1052,1068,1125` (21 klog) and `:270,273` (2 `KTM_FLIGHT`) | delivery/sigreturn audit, `[SIGTERM_AUDIT]`, `USER_FAULT_FRAME`, CLASSIFY-style lines | new `kernel/signal_diag.c`: `signal_trace(event, p, sig, ...)` (IN FLIGHT file: schedule after blocking-linux) |
| `kernel/process/signals.c:83`, `kernel/process/exit.c:55,59,63`, `kernel/process/wait.c:38,68,186,230,306,317` | `[SIGTERM_AUDIT]`, `CLASSIFY ZOMBIE_*`, `[WAIT4_WNOHANG_AUDIT]`, `KTM_CHECKPOINT` | `process_trace(event, ...)` in `kernel/process/sched_trace.c` or a new `process/diag.c` (the existing `wait_audit.c` is the model) |
| `kernel/process/core.c:137,145` | `[ISRABI][IRQ_SAVE]` | same `process_trace` hook |
| `kernel/process/fork.c:40,91,119,132,262,268,277,302,320,656` (10 `KTM_FAULT_HIT`), `:296` (klog), `:351,579` (`KTM_CHECKPOINT`) | fault injection and checkpoints | one `fork_should_fail(site)` hook (Linux `should_fail`) like `mm_diag_fault_hit`, plus `process_trace` |
| `kernel/syscalls/syscall_dispatch.c:518,521,532,539,542` | `KTM_TRACE_SYSCALL_ENTER/RET`, `ktm_probe_diag_syscall_pre/post`, `ktm_user_canary_poll` | one `syscall_trace_enter()` / `syscall_trace_exit()` pair (Linux tracepoints) in a `kernel/syscalls/syscall_diag.c` |
| `kernel/syscalls.c:41,95,100,102` | init-time klog (`syscalls_init`) | KEEP (not hot path) or drop |
| `mm/page_fault_cow.c:56,81,111,147`, `mm/page_fault_kernel.c:36,47,57` | `log_*_fmt("PF", ...)` | `mm_diag_*` hooks (IN FLIGHT: `refactor/mm-diag-headers` already moves `page_fault_kernel.c`; `page_fault_cow.c` is the remaining file) |

Clean: `sched/*`, `sched/switch/context_switch.c`, `kernel_resume.c` (go
through `switch_trace()` in `sched/switch/diag.c`), `mm/page_fault.c`,
`mm/page_fault_user.c`, `mm/paging.c`, `kernel/fault.c`, `kernel/sigtramp.c`,
`kernel/process/signal_enter.c`, `kernel/irq_dispatch.c`.

---

## 5. Other hygiene findings

- Duplicate `#include` in one file (verify each is not inside an `#if` arm):
  `fs/vfs.c` and `fs/devfs.c` (`ir0/arch_cpu.h`), `fs/devfs_internal.h`
  (`string.h`), `ktm/klog.c` (`ir0/clock.h`), `drivers/net/virtio_net.c`
  (`ir0/cpu.h`), `drivers/net/rtl8139.c` (`ir0/ktm/klog.h`),
  `drivers/virtio/virtio_9p.c` (`ir0/cpu.h`, `ir0/ktm/klog.h`),
  `kernel/syscalls/syscall_dispatch.c` (`config.h`).
- Public definitions living in the wrong file: `get_process_fd_table`,
  `fd_slot_*` and `ensure_devfs_init` are defined in
  `kernel/syscalls/io_syscalls.c`.
- `includes/ir0/paging.h` re-exports the whole of `mm/paging.h`.

---

## 6. Prioritized work list

Each item is one red/green (bug) or green/green (refactor) commit series.
Items in the same lane touch disjoint files and can run in parallel; items
in one lane are ordered. "After X" means wait for that concurrent branch.

**Lane U — uaccess (files: `kernel/syscalls/process_syscalls.c` sigaction
part, `fs_meta.c`, `fs_path_syscalls.c`, `fs_syscalls.c`, `time_syscalls.c`,
`epoll_syscalls.c`, `fs_dirent.c`, `io_syscalls.c`, `kernel/process/*.c`)**

1. **U1 (bug, red/green, P0).** `rt_sigaction` touches user memory directly
   after `access_ok` (`process_syscalls.c:1665`, `:1678`): selftest with an
   unmapped `act`/`oldact` (expected `-EFAULT`; today the kernel dereferences it), fix with
   `copy_from_user`/`copy_to_user`. The sigreturn raw read (`:1940`) is the
   same bug; hand it to `refactor/blocking-linux`, which already rewrites that
   code and adds `sigreturn_bad_frame.c`.
2. **U2.** Delete class A' (`access_ok(path, 256)`, 10 sites).
3. **U3.** Delete class A / A'' pre-checks file by file (one commit per
   file), proving each copy returns `-EFAULT`.
4. **U4.** `mm_user_va_ok` callers: signals/mm syscalls -> `access_ok`;
   task IP/SP checks -> one `task_ip_is_user()`; then make `mm_user_va_ok`
   and `mm_user_range_ok` mm/arch-private and add them to
   `[uaccess-single-api]`. Delete `put_user`/`get_user` or adopt them.
   After `refactor/blocking-linux` for `kernel/signals.c` and
   `sched/switch/kernel_resume.c`.

**Lane L — logging (files: `includes/ir0/logging.h`, `kernel/log.h`, every
`LOG_*` / `log_*` caller; mechanical)**

5. **L1.** Replace `LOG_*` (334) and `log_*` (78) with `klog_*`, delete
   both alias layers and `kernel/log.h`, add the retired names to a guard.
   Split by tree (drivers/, fs/, net/, kernel/) to stay clear of other
   lanes; skip `mm/page_fault*` until `refactor/mm-diag-headers` lands.
6. **L2.** Collapse non-`_fmt` klog and `kprintf` into the `_fmt` set;
   delete `kprintf_level`, `klog_get_profile`, `klog_record_count`,
   `klog_get_boot_phase`. Merge `ir0_boot_smoke` and `klog_smoke` (keep
   harness tags stable).

**Lane D — diag hooks (files: `kernel/syscalls/syscall_dispatch.c`,
`kernel/process/{fork,wait,exit,core,signals}.c`, new diag files)**

7. **D1.** `syscall_trace_enter/exit` hook file; move the 5 KTM calls out of
   `syscall_dispatch.c` (after `refactor/blocking-linux`, which touches it).
8. **D2.** `process_trace()` + `fork_should_fail()`; remove the
   `IR0_DEBUG_PROC` / `DEBUG_PROCESS` / `CONFIG_DEBUG_ISRABI` blocks from
   `kernel/process/*`.
9. **D3.** `signal_trace()` in `kernel/signal_diag.c`; remove the 23 inline
   records and 11 `#if` from `kernel/signals.c` (after blocking-linux).
10. **D4.** `mm/page_fault_cow.c` logs -> mm diag hooks (after
    `refactor/mm-diag-headers`).

**Lane R — register names (guarded; files listed in table 3)**

11. **R1.** `arch_task.h` segment accessors -> x86-private header;
    `task_cs_is_user` -> `task_is_user_mode`.
12. **R2.** `elf_loader.c` exec_commit_ctx fields, `oops` `panic_fault_sp`,
    `signal_irq.h` `new_sp`, `stack_watch` `sp`, `fs_path_syscalls.c`
    `remount_flags`, `process.h` `fs_base` member.
13. **R3.** `syscall_frame.h` / `process.h` entry/exit hook parameters
    (`rip_hw`, `stack_r9_slot`, `rax_hw`, `iretq`), arch prototypes into an
    arch hook header. **R4.** `syscall_resume_rax` -> `syscall_retval`
    (after blocking-linux). **R5.** `code_seg` out of the neutral fault
    structs (after mm-diag-headers). **R6.** KTM d1_12/d1_13 diag probes.
    Each item shrinks `scripts/isa_register_name_baseline.txt`.

**Lane S — strings (files: `includes/string.[ch]`, their callers)**

14. **S1.** `kstrlen/kstrcmp/kstrncmp` -> libc names; delete dead
    `strcat/sprintf/strtok/atol/isalnum/isspace/toupper/tolower`.
15. **S2.** `kmemset/kmemcpy/kstrncpy` checked family -> one function each.
16. **S3.** Private `memcmp`/`acpi_memcmp`/`sock_strnlen` -> public
    `memcmp`/`strnlen`; `itoa` -> `snprintf`.

**Lane K — locks (files: `net/*.c`, `fs/named_*.c`, `drivers/tty/console.c`,
`drivers/net/rtl8139.c`, `mm/pmm.c`, `mm/swap/swap.c`, `mm/reclaim.c`,
`ipc/ipc.c`)**

17. **K1.** Per-file `X_irq_save` wrappers -> `ir0_spinlock_t` per
    subsystem (one commit per file; net files after `refactor/net-fs-split`).
18. **K2.** `swap_lock`/`reclaim_lock` -> `ir0_spinlock_t`; delete the
    `#if 0` `ipc_lock`.

**Lane T — time (files: `includes/ir0/clock.h`, `drivers/timer/*`,
`early_*`, `rtc`)**

19. **T1.** Delete the 13 dead `clock_*` entry points plus
    `get_system_time`, `clock_get_uptime_seconds`, `early_clock_quality`,
    `early_time_smoke_ok`, `rtc_write_register`, `rtc_get_*_string`,
    `delay_ms`, `ir0_clock_wait_service_clockevents`; make `get_pit_ticks`
    private.
20. **T2.** fs-local "now" helpers -> `clock_get_current_time`.

**Lane M — mm map/alloc (files: `mm/paging.[ch]`, `mm/pmm.[ch]`,
`includes/ir0/paging.h`, `tlb.h`, `mm_syscalls.c`, `ipc/sysv_shm.c`,
`arch/*/task_stack.c`, `drivers/timer/acpi/acpi_pm.c`) — after
`fix/security-2`**

21. **M1.** Delete `unmap_page`, `map_user_region`,
    `setup_and_enable_paging`, `pmm_init`, the three dead audit functions;
    `pmm_free_frame` -> `pmm_frame_put`; `paging_current_address_space` ->
    `mm_current_root` (or the reverse, one name).
22. **M2.** `pmm_alloc_frame` outside mm -> `mm_alloc_frame`; make
    `mm/pmm.h` mm-private (guard it like the heap backend).
23. **M3.** Review the four direct `mm_activate` callers that bypass
    `mm_prepare_root_activation`; make `mm_activate` arch-private.
24. **M4.** `acpi_pm.c` root decoding -> `map_page_in_directory`.

**Lane F — process/fd lookup (files: `kernel/process.h`, fd users;
overlaps U3/net-fs-split, run after them)**

25. **F1.** `process_get_current/pid/ppid` -> `current_process`.
26. **F2.** `get_process_fd_table` + 38 open-coded bounds checks ->
    `ir0_fd_get/ir0_fd_put`; move `fd_slot_*` out of `io_syscalls.c`;
    delete `ir0_fd_install_product`.

**Lane V — drivers/console/devfs (files: `drivers/`, `includes/ir0/{driver,
init_drv,block_dev,devfs,console,console_backend,typewriter,vga}.h`)**

27. **V1.** Delete 0-caller registration and console entry points listed in
    1.9 and 1.10 (`drivers_init`, `ir0_unregister_driver`,
    `ir0_find_driver`, `ir0_driver_get_state`, `ir0_driver_shutdown_all`,
    `ir0_register_simple_driver`, `devfs_{,un}register_device`, 7
    `ir0_console_*`, 5 `typewriter_*`, dead `print_hex*`, the vga.h
    `LOG_OK/LOG_WARN/LOG_ERR` macros if unused).
28. **V2.** `block_dev_*` -> `ir0_block_*`; `ensure_devfs_init` -> boot-time
    init.
29. **V3.** One screen-output path (vga `print*` family).

**Lane G — signals API shape (after `refactor/blocking-linux`)**

30. **G1.** SIGPIPE/SIGSEGV hand-posting -> `send_signal` /
    one `force_sigsegv`; `signal_deliver_fault` folded into
    `force_sig_fault`; `signals_should_handle_on_run` folded into the one
    pending predicate; delete `signal_ignore`, `signal_syscall_resume.h`.
31. **G2.** `CONFIG_ENABLE_NETWORKING` in `syscall_dispatch.c` and
    `exit_bind.c` -> net registration hook / stub header (after
    `refactor/net-fs-split`).

Suggested first wave in parallel (no shared files, no concurrent-branch
conflict): U1 (rt_sigaction part), L1 (drivers/ and fs/ slices), R1, S1,
T1, V1, K2.
