# IR0 interface review by subsystem

Snapshot of `dev` at `26a3649a` (2026-11-13). Read-only review; no code
changes. It supports the maintainer's own review and **builds on**
`api-variant-inventory.md` (the inventory): items already listed there are
cited by their lane id (U1, L1, D2, M1, F2, G1, ...) and not repeated.

Criteria (from `rules/ir0-api-public-private.md`,
`rules/ir0-scale-within-patterns.md`, `rules/kernel-architecture-rigor.md`
and the maintainer's structure-hygiene rule, sections 1, 3, 4, 5, 6):

- one public API per behavior, only strictly necessary variants, the
  operation validates itself, at most one public predicate;
- each responsibility in its owner subsystem, no reach-ins;
- subsystem directories hold generic code only; ISA code (asm, atomics,
  barriers, registers, MMU) under `arch/<isa>/` behind small facades with
  Linux-like names; directories show where each responsibility lives;
- critical paths: no `#if`, no register names, no inline klog/KTM.

**Paths will move.** Two other branches are reorganizing `kernel/` and
adding `mm/` subdirectories plus an atomics facade while this was written.
Every `file:line` below is as of `26a3649a`; re-locate by symbol name after
those land. Where a finding overlaps that work it is marked *(in flight)*.

## Method and how to read the numbers

- Exported symbols were extracted from every header under `includes/ir0/`
  (and, for reach-in analysis, every private header in the source trees):
  function prototypes, `static inline` functions, `extern` variables and
  function-like macros.
- Callers are grep-level token counts over `arch block drivers fs includes
  ipc kernel ktm mm net sched sound tests cpp`, minus the declaration and
  one definition. Each file is mapped to an owner subsystem by path
  (`kernel/syscalls/erfs_*` and `early_*` are counted as the separate
  "early rootfs" world). "Used outside owner" excludes `tests/` and
  `kernel/test/`.
- "Unused" means no token reference outside its header (and its own
  definition). Inline helpers that are used only by other inlines in the
  same header show up as unused; symbols used only from `rust/` are not
  seen. Struct-field names that collide with function names inflate counts
  (checked by hand where it mattered, e.g. `is_root`). **Re-grep before
  deleting anything.**
- Risk: low = mechanical, no behavior change; medium = behavior-preserving
  refactor across subsystems that needs both-ISA selftests; high = touches a
  blocking/critical path or ABI and needs red/green proof.
- Size: S < 200 changed lines, M 200-1000, L 1000-3000, XL > 3000.

---

## 0. Cross-cutting findings

### 0.1 The public surface in numbers

`includes/ir0/` has **242 headers** exporting **1781 symbols**. Of those,
**939** are used outside their owner subsystem, **587** are used only by
their owner (or only by tests) and **255** have no reference at all
(Appendix A has the per-header table, B the unused list, C the
owner-only list). Read another way: about half of the "public" API is
private API published by accident.

The biggest single contributor is `includes/ir0/process.h`, which is one
line, `#include <kernel/process.h>`: the whole 146-symbol process header
(plus the full `struct process`) is public through it to 78 includers
(see 3.1).

### 0.2 Include search path and basename collisions

`Makefile:308-327` adds 16 include roots, among them `kernel`, `sched`,
`drivers`, `fs`, `net`, `mm` and `arch/arm64/sources`. Any file can
therefore include another subsystem's private header by bare name, and
the same basename resolves differently depending on the search order.
Basenames that exist both as a public facade and as a private header:

`fault.h` (`arch/x86-64/sources/`), `rtc.h` (`drivers/timer/rtc/`),
`ps2_mouse.h`, `console.h` (`drivers/video/`), `dma.h` (`drivers/dma/`, a
different API: ISA 8237 vs `dma_alloc`), `partition.h`, `virtio_9p.h`,
`vfs.h` (`fs/`), `process.h` (`kernel/`), `resource_registry.h`,
`power_manag.h`, `sound_blaster.h`, `paging.h` (`mm/`), `klog.h`
(`ktm/include/`), and `errno.h` three times (`includes/`, `includes/ir0/`,
`kernel/`). `includes/ir0/ktm/klog.h:17` does `#include <klog.h>`, which
only reaches `ktm/include/klog.h` because `ktm/include` precedes
`includes/ir0` in the search order.

### 0.3 Headers that only re-export or aggregate

| Header | Content | Includers | Proposal |
|--------|---------|----------:|----------|
| `includes/ir0/process.h` | `#include <kernel/process.h>` | 78 | real facade (PRC-1) |
| `includes/ir0/vfs.h` | `#include <fs/vfs.h>` | 18 | real facade (FS-6) |
| `includes/ir0/klog.h` -> `ir0/ktm/klog.h` -> `<klog.h>` | two hops to `ktm/include/klog.h` | 16 / 74 | one header (KTM-1) |
| `includes/ir0/net_stack.h` | includes `net/{ip,icmp,dns,arp}.h` | 0 | delete |
| `includes/ir0/arch_cpu.h` | aggregates 12 facades | 19 | delete, include what is used |
| `includes/ir0/arch_port.h` | aggregates 3 facades | 37 | delete |
| `arch/common/arch_interface.h`, `arch_portable.h` | one include each | 1 / 9 | delete |
| `mm/paging.h`, `mm/pmm.h` | include the public header (reverse wrapper) | 1 / 1 | delete |
| `kernel/ipc.h`, `kernel/resource_registry.h`, `kernel/log.h`, `kernel/power/power_manag.h`, `drivers/virtio/virtio_9p.h` | one include each | 0-2 | delete |
| `includes/ir0/smp_convergence.h` | empty | 0 | delete |
| `includes/ir0/abi/musl_cred_abi.h` | no symbols | 0 | delete |

### 0.4 A hidden ISA conditional: `IR0_LEGACY_PC_IO`

`Makefile:1238` defines `IR0_LEGACY_PC_IO=1` exactly when
`ARCH=x86-64`; `includes/ir0/driver_backend.h:17` defaults it to 0. It is an
ISA switch under another name, so the `[common-c-no-isa-conditional]`
guard does not see it. **27 uses in 8 generic files**:
`drivers/input/input_backend.c` (7), `drivers/video/console.c` (5),
`drivers/init_drv.c` (4), `drivers/timer/clock_system.c` (3),
`drivers/char/vga.c` (3), `drivers/net/net_drivers.c` (2),
`drivers/video/vbe.c` (1), `includes/ir0/driver_backend.h` (2).
Proposal (X-1): split each into a per-platform file selected by the build
(or by a platform driver registering itself), then add
`IR0_LEGACY_PC_IO` to the guard's ISA-macro list.

### 0.5 arm64 implements x86 primitives with invented values

`includes/ir0/cpu.h:56-62` and `includes/ir0/arch_io.h` are portable
facades but declare `rdmsr`, `wrmsr`, `cpuid`, `debug_reg_read/write`,
`inb/outb/inw/outw/inl/outl` and `io_wait`. arm64 implements them as
stubs that return 0 (`arch/arm64/sources/interface.c:130-200`). This is
the "do not invent values to fit another platform's API" case of hygiene
section 3: any port-I/O driver built on arm64 silently reads zeros
(sound, bluetooth, USB and the PS/2 mouse are built on arm64 unless the
defconfig turns them off; `setup/configs/hub-rpi4.defconfig:24,26` enables
Bluetooth and the mouse). Proposal (X-4): move MSR/CPUID/debug-register
and port I/O into an x86-only `asm/` header; drivers that need them become
x86-only objects; arm64 gets no stub.

### 0.6 Prototypes with no definition and no caller

15 declarations reference nothing: `arch_info.h` `late_init`,
`get_cmdline`, `get_arch_bits`, `supports_feature`, `get_arch_cflags`,
`get_arch_ldflags`, `dump_cpu_registers`; `page_fault.h`
`get_fault_type`, `get_fault_error`; `cpu_info.h` `get_page_size`,
`get_cpu_mode`; `arch/x86-64/sources/tss.h` `tss_init_x64`,
`tss_load_x64`, `tss_get_address`, `tss_get_size`. Delete (X-5, low, S).

### 0.7 Platform drivers live in arch/ on arm64 and in drivers/ on x86

| Device class | x86-64 | arm64 |
|--------------|--------|-------|
| Serial console (`serial_io.h`) | `drivers/serial/serial.c` (16550) | `arch/arm64/sources/serial.c` + `pl011.c` |
| RTC (`rtc.h`) | `drivers/timer/rtc/rtc.c` (CMOS) | `arch/arm64/sources/rtc.c` (PL031) |
| Timer | `drivers/timer/{pit,hpet,lapic}` | `arch/arm64/sources/timer.c` |
| IRQ controller | `arch/x86-64/sources/interrupt/pic.c` | `arch/arm64/sources/{gic_v2,bcm2836_irq,irq_backend}.c` |
| Keyboard | `arch/x86-64/sources/interrupt/keyboard.c` | `drivers/input/input_backend.c` |
| Power-off (`acpi_pm.h`) | `drivers/timer/acpi/acpi_pm.c` | `arch/arm64/sources/acpi_pm_stub.c` |
| Block (early) | - | `arch/arm64/sources/virtio_blk_early.c` |
| Firmware tables | `drivers/timer/hpet/find_hpet.c` (ACPI) | `arch/arm64/sources/fdt_reader.c` |

The same facade is implemented by an arch file on one ISA and by a driver
on the other, and the x86 PS/2 keyboard driver sits in an arch interrupt
directory. Linux puts device drivers in `drivers/<class>/` for every ISA
(`drivers/tty/serial`, `drivers/rtc`, `drivers/clocksource`,
`drivers/irqchip`) and keeps only CPU/MMU/entry code in `arch/`. See
question Q4.

### 0.8 Blocking is open-coded in 14 places

A task is put to sleep by setting `PROCESS_BLOCKED` by hand in 14 sites
across 9 non-test files: `ipc/ipc.c:89,104`, `drivers/tty/console.c:487`,
`kernel/futex.c:72`, `kernel/signals.c:687,736`,
`kernel/syscalls/io_syscalls.c:90,373,830,1011`,
`kernel/syscalls/process_syscalls.c:492`, `kernel/time/clock_wait.c:89`,
`kernel/process/wait.c:304`, `kernel/process/fork.c:574`. The one
wait-queue primitive that exists (`wait_queue_*`, declared in
`includes/ir0/ipc.h`, defined in `ipc/ipc.c:66-170`) is used only by
`ipc/` and `fs/vfs.c:43`. Proposal (X-7): move it to `sched/wait.c`
(Linux `kernel/sched/wait.c`), give it `wait_event`/`wake_up` semantics
and convert the open-coded sites one at a time. This is the "common core
first" item of `ir0-scale-within-patterns.md`.

### 0.9 Build wiring and manifest drift

- `Makefile:613-657` `LIB_OBJS` holds signals, oops, pipes, tty, syscall
  table, bpivot, blockdev and more: the name says nothing about owners.
- Platform selection is done by `filter-out` lists in the top Makefile
  (`Makefile:660-735`) instead of per-ISA object lists.
- `scripts/kconfig/subsystems.json` calls signals, pipes and oops
  "LIBRARIES".
Proposal (X-9, low, S): per-owner object lists (`SIGNAL_OBJS`, `TTY_OBJS`,
...) and per-ISA platform lists; fix the manifest in the same commit.

### 0.10 Hand-rolled atomics outside the facade *(in flight)*

`__atomic_*` / `__sync_*` outside `arch/`: `mm/oom_kill.c` (8),
`net/tcp.c` (7), `mm/page_table_pool.c` (4), `ktm/klog.c` (3),
`mm/reclaim.c` (2). The atomics-facade branch should take all five files.

---

## 1. sched (core, policies, switch, idle)

Files: `sched/core.c` (81), `sched/core.h` (47), `sched/rr.c` (317),
`sched/prio.c` (363), `sched/idle.c` (48), `sched/switch/context_switch.c`
(57). Clean of `#if`, register names and inline diag.

### 1.1 Public interface

| Header | Symbols | Outside callers |
|--------|--------:|-----------------|
| `sched.h` | 9 | `schedule` 25 (syscalls 8, boot 5, process 4, ...), `sched_add` 14, `sched_promote` 9, `sched_remove` 7 (+13 tests), `sched_take_resched` 2, `sched_switch_to` 1 (exit path), `sched_count_runnable` 1, `sched_policy_name` 2, `sched_selftest` 1 |
| `context.h` | 3 | `switch_to` (sched only), `switch_to_user_task` 2 (syscalls), `switch_to_user` 1 (process) |
| `context_backend.h` | 3 arch hooks | sched only (correct) |
| `idle.h` | 1 | `kernel_idle_loop` 3 (boot) |
| `percpu.h` (owner `kernel/percpu.c`) | 7 | sched 2, process 1, arch 4, mm 1; `cpu_local_by_id` owner-only |

`sched/core.h` (policy contract, `rq_lock`) is private and only included
by `sched/`. Good.

Should be private: `sched_selftest` (a test entry point in the scheduler
facade, defined twice, `sched/rr.c:314` and `sched/prio.c:312`).

### 1.2 Arch boundary

Hooks used: `__switch_to`, `first_switch_to`, `set_current_kernel_stack`
(`context_backend.h`, both ISAs in `arch/*/sources/context.c`,
`arch/x86-64/sources/user_mode.c:151`, `arch/arm64/sources/first_switch.c:19`);
`set_tls` (`tls.h`); `mm_prepare_root_activation` (`arch_mm.h`);
`irq_save/irq_restore`, `cpu_idle`, `enable_interrupts` (`cpu.h`).
No ISA code in `sched/`.

### 1.3 Responsibility leaks

- `sched/idle.c:30-35` `idle_poll()` calls `net_stack_poll()`,
  `ir0_bluetooth_poll()` and `input_kbd_poll_ps2()`: the scheduler knows
  three drivers. Polling devices is a driver concern (a registered poll
  hook or a timer), not the idle loop's.
- `sched/switch/context_switch.c:38-42` fills a task's mm root lazily from
  `process_pgd()` and prepares activation: address-space bookkeeping in the
  switch path (Linux: `switch_mm()` called from `context_switch()`, the mm
  root is set once when the task is created).
- `context_switch.c:43-44` calls `set_current_kernel_stack(next)` and
  `cpu_local_set_kernel_stack(next->kstack_top)` back to back: two calls
  for one behavior.

### 1.4 Simplification candidates

| ID | Proposal | Risk | Size |
|----|----------|------|------|
| SCH-1 | Replace `idle_poll()` with a driver-owned poll list (`driver_poll_register()` in the driver core) or per-driver timers; idle only runs the list | low | S |
| SCH-2 | Add an mm hook `switch_mm(prev, next)` called by `switch_to()`; set `task.mm_root` when the mm is bound (`process_mm_bind`) and drop the lazy fill | medium | S |
| SCH-3 | Fold `cpu_local_set_kernel_stack()` into the arch `set_current_kernel_stack()` (or the reverse) so the switch makes one call | low | S |
| SCH-4 | Move `sched_selftest` out of `sched.h` into the selftest/KTM side; one copy | low | S |
| SCH-5 | Host the wait-queue primitive (X-7) | high | M |

### 1.5 Proposed layout

```text
sched/
  core.c core.h idle.c wait.c        (wait.c from ipc/ipc.c, X-7)
  policy/rr.c policy/prio.c          (CONFIG_SCHEDULER_POLICY picks one)
  switch/context_switch.c
```

---

## 2. signals

Files: `kernel/signals.c` (951), `kernel/process/signals.c`,
`kernel/sigtramp.c`, `kernel/signal_diag.c` + `kernel/signal_diag.h`,
`arch/*/sources/signal.c`. Signal syscalls live in
`kernel/syscalls/process_syscalls.c` (`sys_rt_sigaction`,
`sys_rt_sigprocmask`, `sys_rt_sigsuspend`, `sys_kill`, `sys_tkill`,
`sys_tgkill`, `sys_sigreturn`). Inline diag is gone from `kernel/signals.c`
(inventory D3 done: 0 `#if`, 0 klog, 0 KTM); `kernel/process/signals.c:82`
still has one `IR0_DEBUG_PROC` block.

### 2.1 Public interface

`signals.h`: 20 symbols. Outside users: `signal_pending` 54 (syscalls 38,
process 7, tty 4, net 3, fs 1, time 1), `send_signal` 11, `send_signal_pgrp`
5, `set_current_blocked` 6, `do_signal` 3, `force_sig_fault` 2 (arch),
`signal_exit_current` 2 (mm), `signal_deliver_fault` 1 (mm),
`register_signal_handler` 1, `signals_reset_on_exec` 2,
`signals_try_abandon_sigframe` 1 (syscall dispatch), the 4 sigset inline
converters 2-3 each. Unused: `signal_ignore`. Owner-only:
`signals_on_sigreturn`.
`sigtramp.h`: 2 (process). `signal_irq.h`: 12 arch hooks (see 2.2).
`abi/signal_contract.h` + `asm/signal_contract.h`: ABI constants.

Still open from the inventory (lane G1): `signal_ignore` (unused),
`signal_deliver_fault` beside `force_sig_fault`, the two sigset-converter
pairs.

### 2.2 Arch boundary

`signal_irq.h` (both ISAs, `arch/*/sources/signal.c`): `sigcontext_ip`,
`sigcontext_sp`, `sigcontext_arg`*, `sigcontext_set_arg`*,
`signal_sigcontext_validate_and_sanitize`,
`signal_fill_sigcontext_from_syscall_frame`,
`signal_fill_syscall_frame_from_sigcontext`,
`signal_sigcontext_return_value`, `signal_fill_sigcontext_from_irq_frame`,
`signal_redirect_irq_frame`, `irq_frame_sp`, `signal_set_return`
(*unused; `irq_frame_sp` is used only by a test). Plus `fpu_backend.h`
(`fpu_save_sigcontext`, `fpu_load_sigcontext`, `fpu_signal_enter`,
`fpu_sanitize_sigcontext`), `task_ops.h` `task_load_sigcontext`,
`syscall_frame` accessors. The ledger still lists
`signal_rip_is_registered_handler` and `rip` in `kernel/signals.c`
(`scripts/isa_register_name_baseline.txt`) and `new_rsp` in
`signal_irq.h`.

### 2.3 Responsibility leaks

- Signals reach into the tty: `kernel/signals.c:161`
  (`ir0_console_after_signal_abandon`), `kernel/signals.c:874` and
  `kernel/syscalls/process_syscalls.c:1820`
  (`ir0_console_after_tty_read_signal`). A tty read interrupted by a signal
  should clean up inside the tty read and return `-ERESTARTSYS`; signals
  should not know the tty exists.
- `signals_try_abandon_sigframe()` runs on every syscall entry
  (`kernel/syscalls/syscall_dispatch.c:524`) to detect a `longjmp` out of a
  handler. Linux has no such hook (the kernel keeps no per-frame state that
  needs abandoning). Uncertain whether it can be removed without a frame
  bookkeeping change; needs red/green.
- Signal syscalls live in the process syscall file (see 4.3).

### 2.4 Simplification candidates

| ID | Proposal | Risk | Size |
|----|----------|------|------|
| SIG-1 | Finish lane G1 (delete `signal_ignore`; `signal_deliver_fault` -> static behind `force_sig_fault`; one sigset converter pair) | low | S |
| SIG-2 | tty handles its own interrupted read; delete `ir0_console_after_tty_read_signal` / `ir0_console_after_signal_abandon` and the three call sites | medium | S |
| SIG-3 | Remove the per-syscall `signals_try_abandon_sigframe()` by not keeping abandonable frame state (Linux model) | high | M |
| SIG-4 | `signal_irq.h`: delete `sigcontext_arg/set_arg`, make `irq_frame_sp` test-local, rename toward Linux (`setup_sigcontext`, `restore_sigcontext`, `setup_rt_frame`), `new_rsp` -> `new_sp`, raw `uint64_t *frame` -> an opaque `struct pt_regs`-like type | low | S |
| SIG-5 | Move the signal syscalls next to the signal code | low | M |

### 2.5 Proposed layout

```text
kernel/signal/
  signal.c            (kernel/signals.c)
  exit.c              (kernel/process/signals.c: default actions, fatal exit)
  sigtramp.c diag.c diag.h
  syscalls.c          (rt_sigaction, rt_sigprocmask, rt_sigsuspend, kill, tkill, tgkill, sigreturn)
```

---

## 3. process (lifecycle, fork/exec/exit/wait, creds, files)

Files: `kernel/process/` (33 files, 4503 lines, flat), plus
`kernel/process.h` (146 symbols), `kernel/process.c` (32-line "legacy
stub"), `kernel/elf_loader.c` (2099), `kernel/elf64_image.c`,
`kernel/early_elf_stack.c`, `kernel/exec_read_trace.c`, `kernel/futex.c`,
`kernel/kexec.c`.

### 3.1 Public interface

`includes/ir0/process.h` re-exports `kernel/process.h`: **146 symbols, 78
includers**. Breakdown: 76 used outside process (not counting tests), 54
used only by process or by tests, 16 unused
(`fork_restore_emit_pre_iretq`, `fork_flow_note_kernel_entry`,
`process_guest_install`, `process_guest_rearm`, `process_exit_guest`,
`process_sched_state_trace`, `process_has_zombie_child`,
`process_count_resident_user_pages`, `process_list_count`,
`process_list_count_user`, `process_set_pgd`, `process_syscall_flags`,
`process_syscall_set_ip/sp/flags/arg`). Heaviest outside users:
`current_process` (~780 refs outside process, not counting tests), `process_pgd` (131: syscalls
45, mm 40, tests 30), `process_find_by_pid` (46, 25 of them tests), `process_set_sched_state`
(43: syscalls 17, sched 8, ipc 5, tty 3, signals 3, time 3),
`process_get_current` (20: mm 12, arch/x86 4), `process_heap_*` /
`process_stack_*` / `process_mmap_*` accessors (syscalls, mm, ktm, arch).

Other process-owned headers (symbols / outside-used / owner-only / unused):
`credentials.h` 5/4/0/1, `permissions.h` 6/4/0/2 (split fs/process),
`cred_transition.h` 3/2/0/1, `cwd.h` 2/1/0/1, `files_struct.h` 11/4/7/0
(4 defined in `kernel/syscalls/io_syscalls.c`), `fd_get.h` 3/3/0/0,
`fd_install.h` 1/0/0/1, `fd_dispatch.h` 6/5/0/1, `mm_struct.h` 12/4/7/1,
`process_domains.h` 5/0/5/0, `process_exit.h` 3/1/2/0,
`process_introspect.h` 2/2/0/0 (ktm only), `process_limits.h` 3/3/0/0,
`futex.h` 4 (syscalls 3, rest tests), `kexec.h` 3/3, `elf_loader.h` 5/2/0/3,
`elf64_image.h` 2/1/1/0, `exec_read_trace.h` 6/6 (all called from fs),
`vfs_file.h` 6/4/2/0, `clone.h`, `wait.h` 5 (one unused inline).

Whole-file private candidates: `process_domains.h` (5 owner-only),
`process_exit.h` (2 of 3 owner-only).

### 3.2 Arch boundary

Arch hooks consumed: `fork.h` (`fork_prepare_parent_return`,
`fork_prepare_child_return`, `process_set_tls`), `task_ops.h` (9, two
unused: `task_retval_slot_addr`, `task_store_sigcontext`),
`arch_task.h` (16 inlines, five unused; the x86 segment accessors
`task_get/set_cs/ss`, `task_cs_is_user` are inventory R1), `syscall_frame.h`
(3), `arch_debug.h` `debug_state_init`, `arch_elf.h` (3), `tls.h`,
`fpu.h` `fpu_flush_thread`, `vdso.h` `vdso_map`.

Hooks implemented by process but called by arch, declared in
`kernel/process.h`: `process_capture_syscall_frame_at_entry` (arch 4),
`process_syscall_restore_exit_regs` (arch 3),
`process_apply_syscall_frame_to_task`,
`process_sync_task_user_ip_from_syscall_frame`,
`process_save_user_exception_frame`, `fork_ret_first_syscall_entry`
(`rax_hw, rip_hw, rsp_hw` parameters, ledger), and two unused ones,
`fork_restore_emit_pre_iretq` and `fork_flow_note_kernel_entry` (the
latter still defined in `arch/x86-64/sources/fork_asm_hooks.c:51`).

ISA concepts in generic process code: `process_restore_user_task_segments`
(`kernel/process/core.c:193-198`) -> `task_apply_user_segments` (x86
segments, called from the syscall exit, `syscall_dispatch.c:498`);
4-level page-table walks in `kernel/process/mm.c:86-290`
(`process_pt_child`, nested `i4/i3/i2` loops, 14 PTE-helper calls).

### 3.3 Responsibility leaks

- **`struct process` is fully public** (67 fields). Distinctive fields read
  or written outside `kernel/process/` (grep of `->field`/`.field`):
  credentials `uid/gid/euid/egid/suid/sgid` ~190 (syscalls ~95, fs ~78),
  `cwd`/`root` ~85 (syscalls 75), `comm` 71 (ktm 28, fs 16, oops 8),
  `it_real_*` 24 (syscalls), `signal_*` masks ~160 (signals, but also
  syscalls, net, mm, ktm), `kstack_base/top` 22 (arch), `syscall_frame` 25.
- **Four access-check APIs and three root predicates.**
  `check_file_access(path, mode, p)` (`kernel/process/credentials.c:98`,
  path based, calls `vfs_stat`, so process includes `fs/vfs.h`),
  `ir0_check_file_access(path, mode)` (`:72`), `ir0_access_from_stat`
  and `ir0_access_from_stat_groups` (`fs/permissions.c:42,90`).
  Root: `is_root(p)` (0 callers; the 124 grep hits are struct fields),
  `ir0_cred_is_root()`, `early_process_is_root()`. `get_current_uid/gid`
  (`fs/permissions.c:25,34`) have 0 callers next to `ir0_current_cred()`.
- **mm work in process:** page-table walkers and counters in
  `kernel/process/mm.c`; `mm_struct` lifecycle in
  `kernel/process/mm_struct.c` (Linux keeps both in `mm/` and
  `kernel/fork.c` only calls them); `find_vma`/`vma_is_shared` are declared
  in `kernel/process.h` and used by mm (6/2). `mm/paging_fork.c:100,136`
  declares `extern process_unmap_user_pages_all(...)` inside a function to
  call into process.
- **fd table split across three owners:** `files_struct` (process),
  `fd_resource` (fs), and `get_process_fd_table()`, `fd_slot_note_*`,
  `fd_slot_stats_get` and `ensure_devfs_init()` defined in
  `kernel/syscalls/io_syscalls.c:916,929,938,1158,1196` (inventory F2).
  `vfs_file.c` (the open-file object) lives in `kernel/process/`.
- **Exit notifies other subsystems by name:**
  `kernel/process/exit_bind.c:32-71` calls the tty
  (`ir0_console_purge_waiters_for_process`, `ir0_console_clear_fg_pgid`,
  `ir0_console_clear_ctty_session`, ...), ipc
  (`ipc_purge_waiters_for_process`) and TCP (`tcp_wire_on_process_exit`,
  through the private `net/tcp.h`, `:15`), inside
  `#if CONFIG_ENABLE_NETWORKING` (`:17`, `:84`). In Linux, closing the
  files releases sockets and the tty handles session exit.
- **Inline diag** (inventory D2, still open): `kernel/process/fork.c` 12
  KTM refs, `core.c:103,111` (`CONFIG_DEBUG_ISRABI`), `wait.c:67`,
  `exit.c:56`, `signals.c:82`.

### 3.4 Simplification candidates

| ID | Proposal | Risk | Size |
|----|----------|------|------|
| PRC-1 | Split `kernel/process.h`: the owner-only symbols (up to 54, minus what tests need) to `process_internal.h`, delete the 16 unused, then make `ir0/process.h` a real facade (`current`, lookup, state, accessors) instead of a re-export | medium | L |
| PRC-2 | Encapsulate the distinctive fields: `struct cred` + `current_cred()` for ids, an `fs_struct` (cwd/root) owned by process, accessors for `comm`, itimers in time, stack bounds via arch-neutral accessors | medium | L |
| PRC-3 | One credential/permission API: keep `ir0_current_cred()` and one stat-based check (Linux `generic_permission`) in fs; delete `is_root`, `get_current_uid/gid`, `check_file_access`, `ir0_check_file_access`; merge `credentials.h`, `permissions.h`, `cred_transition.h` into one header | low | S |
| PRC-4 | Move page-table walking (`kernel/process/mm.c`) and `mm_struct` (`mm_struct.c`, `mm_struct.h`) into `mm/` | medium | M |
| PRC-5 | One fd-table owner: `files_struct` API in process, defs out of `io_syscalls.c`; `vfs_file.c` to fs (lane F2) | medium | M |
| PRC-6 | Replace `exit_bind.c` name-calls with resource release on file close plus one tty session-exit hook; drop the `net/tcp.h` include and the `#if` | medium | M |
| PRC-7 | Move arch->process hook prototypes out of `kernel/process.h` into one arch-hook header with neutral names; delete the two unused ones (shrinks the register-name ledger) | low | S |
| PRC-8 | Delete `kernel/process.c` (legacy stub) | low | S |
| PRC-9 | Lane D2 (diag hooks for fork/wait/exit/core) | low | M |

### 3.5 Proposed layout

```text
kernel/process/
  core/      core.c registry.c registry_lock.c lifecycle.c create.c saved_context.c saved_environ.c
  fork/      fork.c parent.c domains.c
  exit/      exit.c exit_bind.c wait.c wait_state.c wait_audit.c resources.c
  exec/      exec.c elf_loader.c elf64_image.c early_elf_stack.c   (see Q6)
  cred/      credentials.c cred_groups.c limits.c
  files/     fdtable.c fdtable_init.c files_struct.c files_lifecycle.c cwd.c pseudo_fd_bind.c
  diag/      sched_trace.c process_introspect.c exec_read_trace.c
  futex.c kexec.c
```
(`mm.c`, `mm_struct.c` go to `mm/`; `vfs_file.c` to `fs/`.)

---

## 4. syscalls (entry, dispatch, table) and the early rootfs world

Files: `kernel/entry.c` (52), `kernel/syscalls.c` (104),
`kernel/syscalls/syscall_dispatch.c` (573), `syscall_table.c`,
`syscall_identity.c`, plus 20 implementation files (about 13.3k lines) and
32 early-rootfs files (about 11.9k lines).

### 4.1 Public interface

| Header | Symbols | Notes |
|--------|--------:|-------|
| `entry.h` | 1 | `irqentry_exit_to_user_mode` (arch 2). Good, Linux name |
| `syscall_id.h` | 1 arch hook | `syscall_decode_number` |
| `syscall_frame.h` | 3 arch hooks | `syscall_capture_frame_at_entry`, `syscall_restore_exit_regs`, `exception_frame_is_user` (+ the `rip_hw` ledger entry) |
| `syscall_table.h` | 5 | `syscall_handler_set`, `syscall_context_handler_set` unused; the context-provider machinery is used only by the early dispatcher |
| `syscall_identity.h` | 2 | both unused |
| `syscall.h` | 58 | 57 unused userspace-style `ir0_*` wrappers; `syscall()` inline; included by 3 kernel files only for the `SYS_*` names |
| `syscall_x86_64.h`, `syscall_arm64.h` | 10 / 5 | inline-asm userspace syscall stubs (`svc #0`, `syscall`) in the kernel facade directory; used only by `syscall.h` |
| `kernel/syscalls.h` (private) | 3 | `stdin_wake_check` called by the x86 keyboard driver |
| `io_async.h`, `poll.h`, `epoll.h`, `select.h`, `uio.h` | 1/2/3/4/0 | `poll_wake_check*` are used by net (15) and fs (3): a wait primitive in disguise (X-7) |

### 4.2 Critical path (`syscall_dispatch()`, `syscall_dispatch.c:509-573`)

- Inline diag: `KTM_TRACE_SYSCALL_ENTER/RET` (`:540`),
  `ktm_probe_diag_syscall_pre/post` (`:564`), `d1_12_read_diag_*` x2,
  `ktm_user_canary_poll` (`:561`): 9 references (inventory D1).
- `signals_try_abandon_sigframe()` (`:524`) and
  `fork_ret_first_syscall_entry()` (`:528`) on every entry.
- `exit_to_user_mode()` special-cases fork and clone (`:490`) to wake the
  child, and calls `process_restore_user_task_segments()` (`:498`), an x86
  concept by name.
- `#if CONFIG_ENABLE_NETWORKING` at `:73`, `:98`, `:444` (inventory G2).
- The table is 168 assignments, 17 hand-written wrappers and the `WRAP0..6`
  macros; four rows are IR0-specific non-Linux syscalls
  (`wrap_console_scroll`, `wrap_console_clear`, `wrap_keymap_set/get`,
  `:263-285`).

### 4.3 Responsibility leaks

- **Every subsystem's syscalls live here.** fs: `fs_syscalls.c`,
  `fs_rw.c`, `fs_meta.c`, `fs_dirent.c`, `fs_path_syscalls.c` (3895
  lines); mm: `mm_syscalls.c`, `mm_vma.c`, `mm_brk.c` (~1830, including
  the VMA list and 25 PTE-helper calls); net: `socket_syscalls.c` (2040);
  process/creds/signals: `process_syscalls.c` (1824); tty/pipe/poll/dup:
  `io_syscalls.c` (1891, also defines fd-table and devfs-init APIs);
  `epoll_syscalls.c`, `time_syscalls.c`, `swap_syscalls.c`. Linux keeps each
  next to its owner (`fs/read_write.c`, `mm/mmap.c`, `net/socket.c`,
  `kernel/signal.c`, ...), so the owner's internals stay private. Here the
  syscall layer is the biggest consumer of other subsystems' internals
  (the distinctive `struct process` fields of 3.3, `devfs_pty_*` 13, `sock_stream_*` ~60,
  `pseudo_fs_*`, `proc_*`, `named_*`).
- `kernel/syscalls.c` holds tty stdin logic (`syscalls_read_stdio_stdin`,
  `stdin_wake_check[_nosched]`), called from
  `arch/x86-64/sources/interrupt/keyboard.c:321`.

### 4.4 The early rootfs world (erfs / early_*)

A second, parallel implementation of syscalls, VFS, process state, pipes,
sockets, eventfd/memfd/timerfd/epoll and time used before the common VFS
is up: `kernel/syscalls/erfs*.c` + `early_*.c` (32 files, ~11.9k lines),
`ipc/early_unix.c`, `ipc/early_anonfd.c`, `ipc/product_ipc.c`,
`fs/ramtab.c`, `kernel/syscalls/product_vfs.c`; its own dispatcher
(`early_dispatch.c`, using `syscall_context_provider_register`). Built on
both ISAs (`Makefile:439-470`); the arm64 boot drives it
(`arch/arm64/sources/boot_port.c:60-77`).

Public headers: `early_rootfs.h` (172 symbols: 10 used outside the early
world, 142 only inside it or by tests, 20 unused),
`early_process.h` (31, 16 unused), `early_anonfd.h`, `early_unix.h`,
`early_io.h`, `early_mm.h`, `early_vfs.h`, `early_time.h`,
`early_syscall.h`, `early_cpio.h`, `product_vfs.h`, `product_ipc.h`,
`bpath.h`, `bpivot.h`, `ramtab.h`, `vfs_dispatch.h`, `fd_install.h`,
`cwd.h`, `syscall_identity.h`. Eleven `*_provider` variables
(`early_io_provider`, `early_mm_provider`, `early_vfs_provider`,
`early_time_provider`, `ir0_product_vfs_provider`,
`ir0_product_ipc_provider`, `ir0_product_identity_provider`,
`ir0_product_cwd_provider`, ...) are declared and never referenced.
`kernel/syscalls/erfs_internal.h` (92 symbols) uses generic global names
(`alloc_fd`, `close_fd`, `fd_is_open`, `node_of_fd`, `g_ready`); four of
them (`unix_find_bound`, `g_ready`, `g_cwd_ops`, `g_user`) are also
defined, `static` or not, in `net/`, `drivers/`, `kernel/process/` or tests.

This is the largest "two implementations of one behavior" in the kernel.
Decision needed (Q1).

### 4.5 Simplification candidates

| ID | Proposal | Risk | Size |
|----|----------|------|------|
| SYS-1 | Move syscall implementations into their owner subsystems (or into owner subdirectories, see Q2); `kernel/syscalls/` keeps only entry, dispatch and the table | medium | XL (mechanical, many files) |
| SYS-2 | `syscall_trace_enter()/syscall_trace_exit()` in a `syscall_diag.c`; dispatcher calls only those (lane D1) | low | S |
| SYS-3 | Fork child wake at the end of `sys_fork/sys_clone` (Linux wakes in `kernel_clone()`), segment restore inside the x86 syscall exit; `exit_to_user_mode()` keeps resched + signals only | medium | S |
| SYS-4 | One table mechanism: generate the native table from one list (X-macro), drop the context-provider path once the early dispatcher goes, delete the two unused setters and `syscall_identity.h` | low | M |
| SYS-5 | net registers its syscall rows (or a stub file when networking is off); no `#if` in the dispatcher (lane G2) | low | S |
| SYS-6 | Move `syscall.h`'s userspace wrappers and `syscall_{x86_64,arm64}.h` to the userspace library / `includes/uapi`; kernel files take `SYS_*`/`__NR_*` from uapi | low | S |
| SYS-7 | tty stdin functions from `kernel/syscalls.c` to `drivers/tty/` | low | S |
| SYS-8 | Early rootfs world: see Q1 (retire, freeze, or fold into fs as an initramfs filesystem) | high | XL |
| SYS-9 | Console/keymap custom syscalls -> tty ioctls (Linux `TIOCL*`, `KDSKBENT`) | medium | S |

### 4.6 Proposed layout

```text
kernel/entry/        entry.c syscall_dispatch.c syscall_table.c syscall_diag.c
kernel/syscalls/     (empty once SYS-1 lands, or per-domain subdirs if Q2 keeps them central)
fs/early/ or init/   (whatever Q1 keeps of erfs)
```

---

## 5. mm *(subdirectories in flight)*

Files: `mm/` (30 files + `mm/swap/`, 6837 lines). mm-related code
outside `mm/`: `kernel/syscalls/mm_*.c`, `kernel/process/mm.c`,
`kernel/process/mm_struct.c`, `drivers/timer/acpi/acpi_pm.c:112-134`.

### 5.1 Public interface

| Header | Symbols | Outside used | Owner-only | Unused | Notes |
|--------|--------:|-------------:|-----------:|-------:|-------|
| `paging.h` | 41 | 29 | 10 | 2 | `setup_and_enable_paging`, `unmap_page` unused |
| `pmm.h` | 17 | 8 | 8 | 1 (`pmm_init`) | `pmm_alloc_frame` used by ipc, syscalls, arch/x86 (bypasses reclaim, lane M2) |
| `copy_user.h` | 10 | 7 | 0 | 3 (`clear_user`, `put_user`, `get_user`) | `access_ok` 11 left outside mm (lane U3) |
| `kmem.h` | 2 macros + fns | yes | | | |
| `mm_alloc.h` | 1 | 0 | 1 | | `mm_alloc_frame` is mm-only today |
| `page_table_pool.h` | 2 | arch | | | |
| `boot_memory.h` | 11 | 5 | 5 | 1 | |
| `swap.h` | 19 | 7 | 12 | 0 | |
| `oom.h` | 11 | 3 | 8 | 0 | 2 symbols defined in process |
| `memfd.h` | 9 | 8 | 1 | 0 | |
| `reclaim.h`, `tlb_sync.h`, `mm_diag.h` | 2/2/1 | 0 | all | | whole header private |
| `mm_port.h` | 4 | 4 | | | stats for procfs; KTM mock |
| `no_mmu.h` | 4 | 0 | 0 | 4 | only host tests use it |
| `mm_range.h` | 2 | arch | | | lane U4 |
| `abi/mmap_contract.h`, `abi/brk_contract.h`, `mman.h` | 15/2/0 | 3 | | | most mmap-contract inlines are internal to the header |
| arch: `arch_mm.h` (34), `mm.h` (5), `tlb.h` (4), `page_fault.h` (5), `cacheflush.h` (1), `dma.h` (5), `arch_pf_debug.h` (2) | | | | | see 5.2 |
| process-owned `mm_struct.h` | 12 | 4 | 7 | 1 | |

### 5.2 Arch boundary

`arch_mm.h` mixes three kinds of hooks: address-space layout
(`mm_kernel_heap_*`, `mm_user_va_*`), translation control
(`mm_enable_translation`, `mm_translation_enabled`, `mm_translation_ready`,
`mm_prepare_root_activation`, `mm_create/destroy_user_root`) and raw PTE
encoding (`mm_pte_present`, `mm_pte_phys`, `mm_pte_mapping_flags`,
`mm_pte_executable`, `mm_make_*_pte`, `pte_present/user/write/cow/protnone`,
`mm_va_indices`, `mm_table_entries`, ...). The PTE part is used outside mm
by `kernel/syscalls/mm_syscalls.c` (24 calls), `kernel/process/mm.c` (14)
and `mm_vma.c` (1). `tlb.h` (`mm_activate`, `mm_current_root`,
`tlb_invalidate_page`, `tlb_invalidate_all`) is consumed by syscalls (2),
video (1), time (1), process (1): `mm_activate` is still public (lane M3).

ISA-shaped generic code: 4-level walks (`mm/paging_fork.c:61-160`,
`kernel/process/mm.c`), fixed 4 KiB masks (`paging.h:49-58`
`PAGE_FRAME_MASK ~0xFFF`), and the `PAGE_*` alias layer
(`paging.h:24-46`, every flag has two names: `PAGE_RW` = `IR0_MM_MAP_WRITE`).
Hand-rolled atomics in `oom_kill.c`, `page_table_pool.c`, `reclaim.c`
(0.10, in flight).

### 5.3 Responsibility leaks

- VMA management, mmap/munmap/mprotect/brk and page walks live in
  `kernel/syscalls/` and `kernel/process/` (5.2). `find_vma` is declared in
  `kernel/process.h`.
- `drivers/timer/acpi/acpi_pm.c:112-134` decodes `mm_current_root()` as
  `cr3`/`pml4` (lane M4).
- `mm/paging_fork.c:100,136` `extern` declaration of a process-private
  function inside a function body.
- `includes/ir0/paging.h:22` includes `copy_user.h` (unrelated facade).
- `mm/mm_port.c` produces procfs statistics; acceptable as an mm export,
  but it is reached through a KTM-mockable port (`mm_port.h`) that only
  procfs and syscalls consume.

### 5.4 Simplification candidates

| ID | Proposal | Risk | Size |
|----|----------|------|------|
| MM-1 | Move `mm_syscalls.c`, `mm_vma.c`, `mm_brk.c` and `kernel/process/mm.c` walkers into `mm/` (`mm/mmap.c`, `mm/vma.c`, `mm/brk.c`, `mm/walk.c`); then the PTE section of `arch_mm.h` becomes mm-private (an `asm/pgtable.h`-style header only `mm/` and `arch/` include) | medium | L |
| MM-2 | Finish lanes M1-M4 (dead `setup_and_enable_paging`, `unmap_page`, `pmm_init`; `pmm_alloc_frame` callers -> `mm_alloc_frame`; `mm_activate` private; `acpi_pm` root decode) | low | S |
| MM-3 | Make the owner-only headers private (`tlb_sync.h`, `mm_diag.h`, `reclaim.h`, `no_mmu.h`) and the owner-only symbols of `paging.h`, `pmm.h`, `swap.h`, `boot_memory.h` static or mm-internal | low | S |
| MM-4 | Atomics through the facade *(in flight)* | low | S |
| MM-5 | One flag namespace (`PAGE_*` or `IR0_MM_MAP_*`), add the other to the retired-name ledger | low | M |
| MM-6 | `mm_struct` owned by mm (with PRC-4) | medium | M |

### 5.5 Proposed layout

```text
mm/
  alloc/     kmem.c allocator.c alloc_policy.c pmm.c page_table_pool.c
  paging/    paging.c paging_identity.c paging_fork.c paging_audit.c tlb_sync.c walk.c
  fault/     page_fault.c page_fault_user.c page_fault_kernel.c page_fault_cow.c diag.c
  uaccess/   copy_user.c paging_uaccess.c user_va.h
  vma/       mm_struct.c vma.c mmap.c brk.c memfd.c
  reclaim/   reclaim.c oom.c oom_kill.c swap/
  boot_memory.c no_mmu.c mm_port.c
```

---

## 6. fs / VFS

Files: `fs/` (70 files, 26.8k lines, flat). VFS core: `vfs.c`,
`vfs_mount.c`, `vfs_namei.c`, `vfs_file.c`, `vfs_dispatch.c`,
`vfs_internal.h`, `vfs.h`. Filesystems: minix (6 files), tmpfs, fat16,
ext2, simplefs, hostshare_9p. Pseudo: devfs (12 files), procfs (5),
sysfs, heartfs, pseudo_fs (2), named_{fifo,socket,symlink,devnode}. Other:
pipe, eventfd, timerfd, flock, path*, bpath/bpivot, ramtab,
fd_resource(+_legacy), permissions, chmod, utimens, open_flags, stat_user.

### 6.1 Public interface

fs owns about 35 headers in `includes/ir0/`. The large ones:

| Header | Symbols | Outside used | Owner-only | Unused |
|--------|--------:|-------------:|-----------:|-------:|
| `devfs.h` | 53 | 19 | 33 | 1 |
| `procfs.h` | 51 | 4 | 41 | 6 (`proc_open/read/write/get_offset/set_offset/getdents`) |
| `sysfs.h` | 41 | 12 | 25 | 4 |
| `pseudo_fs.h` | 22 | 6 | 11 | 5 |
| `pipe.h` | 16 | 13 | 1 | 2 |
| `ramtab.h` | 15 | 11 (early world) | 3 | 1 |
| `pty_devfs.h` | 14 | 13 | 1 | 0 |
| `bpivot.h` | 13 | 6 | 6 | 1 |
| `path_routed.h` | 8 | 8 | 0 | 0 |
| `fs/vfs.h` (via `ir0/vfs.h`) | 36 | 27 | 7 | 2 |

`devfs.h`, `procfs.h`, `sysfs.h` export every per-file handler
(`dev_null_read`, `proc_meminfo_read`, `sys_kernel_version_read_reg`, ...)
that only their own registration table uses.

### 6.2 Arch boundary

`fs/procfs_system.c:270-345` decodes x86 CPUID feature bits (EDX/ECX
tables) to build `/proc/cpuinfo` and calls the x86-shaped `cpu_info.h`
(`get_cpuid_max_leaf`, `get_cpu_feature_bits`, `get_cpu_clflush_size`,
`get_cpu_brand_string`). Linux: per-arch `show_cpuinfo()`. `fs/vfs.c` and
`fs/devfs.c` include `arch_cpu.h` twice (inventory 5). Otherwise no ISA
code.

### 6.3 Responsibility leaks

- **Pseudo filesystems are not VFS mounts.** The syscall layer routes by
  path prefix: `kernel/syscalls/fs_syscalls.c:652-706` (`is_proc_path`,
  `is_sys_path`, `is_heart_path`, `ir0_is_dev_path`, then
  `*_is_virtual_subdir`), `fs_dirent.c:82-95`, `fs_meta.c:31`. The proc
  fstype is registered with `.ops = NULL` (`fs/vfs_mount.c:163-168`).
  `path_routed.h` adds 8 `ir0_*_path_routed` helpers
  (`ir0_stat_path_routed` 16 callers) and four `named_*` side tables store
  FIFOs, sockets, symlinks and device nodes by path rather than as VFS
  inodes. In Linux each of these is a `file_system_type` with inode and
  file operations, mounted at `/proc`, `/sys`, `/dev`, and `mknod`/
  `mkfifo`/`bind`/`symlink` create inodes in the parent filesystem.
- **Network control plane in fs.** `fs/devfs_net.c` (757 lines) implements
  ping (`icmp_send_echo_request`, `:48`), DNS lookups (`:179-196`) and IP
  configuration by writing the net globals `ip_gateway`, `ip_netmask`,
  `ip_local_addr` (`:56-66`); `fs/procfs_net.c` reads them. These belong to
  net, exposed through a small config API.
- **Shell UI inside a filesystem driver.** `fs/minix_dir.c:209-340`
  (`minix_fs_ls`, printing listings with `klog_print("SERIAL: ...")`) and
  the `typewriter_vga_print` macro (`fs/minix_fs_internal.h:29`, writes to
  the console) plus 12 unused minix functions (`minix_fs_cat`,
  `minix_fs_read_file`, `minix_fs_cleanup`, ...).
- **Process diag in the read path:** `exec_read_trace_*` (process-owned)
  is called 14 times from `fs/vfs*.c` and `fs/minix_*.c`.
- **Driver-specific readers called from fs:** 11 Bluetooth entry points
  (`ir0_bt_proc_*`, `ir0_bt_sysfs_*`, `ir0_bt_hci_*`), `audio_backend_*` 6, `input_mouse_*` 5,
  `video_backend_get_info`, `ir0_fb_*`: drivers should register their own
  pseudo nodes.
- `drivers/storage/fs_types.c` (filesystem-type detection) lives in the
  storage driver directory.
- `pipe.h` publishes `pipe_ktm_note_read_sleep/read_wake/write_wake`.

### 6.4 Simplification candidates

| ID | Proposal | Risk | Size |
|----|----------|------|------|
| FS-1 | Make devfs, procfs, sysfs and heartfs real `vfs_fstype`s with ops, mounted at boot; VFS lookup crosses the mount; delete `is_*_path`, `*_is_virtual_subdir`, `path_routed.h`; then fold the `named_*` tables into inodes of the parent fs (Q3) | high | XL |
| FS-2 | `devfs.h`, `procfs.h`, `sysfs.h`, `pseudo_fs.h`: handlers `static` in their files, headers keep only registration and lookup; delete the 16 unused | low | M |
| FS-3 | Net config API (`net_if_get_config/set_config`, `net_ping`) owned by net; `devfs_net.c` and `procfs_net.c` become thin adapters | medium | M |
| FS-4 | Delete the minix shell UI and its 12 unused helpers | low | S |
| FS-5 | `exec_read_trace_*` -> one fs diag hook (or delete if the investigation is closed) | low | S |
| FS-6 | One VFS facade: real `ir0/vfs.h` (not a re-export); `vfs_backend.h`, `vfs_dispatch.h` (early world) and `vfs_file.h` (process) folded or made private | low | M |
| FS-7 | Drivers register their pseudo nodes (Bluetooth, audio, input, fb) instead of fs calling them | medium | M |
| FS-8 | `fs_types.c` to fs; `pipe_ktm_note_*` private | low | S |

### 6.5 Proposed layout

```text
fs/
  vfs/        vfs.c vfs_mount.c vfs_namei.c vfs_file.c vfs_dispatch.c vfs_internal.h path.c path_user.c permissions.c open_flags.c stat_user.c utimens.c chmod.c flock.c mount_prefix.c
  minix/ ext2/ fat16/ tmpfs/ simplefs/ 9p/
  devfs/      devfs*.c devfs_internal.h
  procfs/     procfs*.c procfs_internal.h
  sysfs/ heartfs/ pseudo/ (pseudo_fs*.c)
  pipe/       pipe.c named_fifo.c
  anon/       eventfd.c timerfd.c
  boot/       bpath.c bpivot.c bpivot_boot.c ramtab.c        (see Q1)
```

---

## 7. net

Files: `net/` (25 files, 10k lines, flat). Socket syscalls in
`kernel/syscalls/socket_syscalls.c` (2040).

### 7.1 Public interface

| Header | Symbols | Outside used | Owner-only | Unused |
|--------|--------:|-------------:|-----------:|-------:|
| `net.h` | 39 | 26 | 10 | 3 (`net_unregister_protocol`, `ir0_host_to_be16/32`) |
| `sock_stream.h` | 40 | 34 | 3 (tests only) | 3 |
| `sock_icmp.h` | 12 | 11 | 1 | 0 |
| `sock_udp.h` | 9 | 9 | 0 | 0 |
| `sock_inet_ioctl.h`, `socket.h` (CMSG macros) | 1 / 4 | yes | | |
| `net_stack.h` | 0 | 0 includers | | delete |

`net.h` also exports three globals, `ip_local_addr`, `ip_netmask`,
`ip_gateway` (fs uses 19 times).

Private headers reached from outside: `net/tcp.h` from
`kernel/process/exit_bind.c:15`; `kernel/log.h` from
`net/sock_stream.c:20`.

### 7.2 Arch boundary

None in `net/`. `net_stack_get_irq_line` / `net_stack_handle_irq` are
called by the x86 interrupt code (`arch/x86-64/sources/interface.c:309`,
`isr_handlers.c:475`) instead of the NIC driver calling `irq_request`.

### 7.3 Responsibility leaks

- **No socket layer.** `socket_syscalls.c` decides the protocol per fd
  with `sock_stream_is` (9), `sock_icmp_is` (6), the UDP checks, and
  `sock_stream_family/type` (6/6), then calls the protocol module
  directly. Linux: `struct socket` with `proto_ops` in `net/socket.c`.
- A hand-rolled busy lock `g_tcp_connect_busy` (`net/tcp.c:29`,
  `__sync_lock_test_and_set` loop at `:401`, releases at `:65,420-462`) on a
  connect path that can wait.
- `extern` prototypes inside `.c` files: `net/udp.c:249`,
  `net/arp.c:548`, `net/net.c:573` (weak).
- Network config and ping in fs (FS-3); process exit calls TCP (PRC-6);
  idle loop polls the stack (SCH-1).

### 7.4 Simplification candidates

| ID | Proposal | Risk | Size |
|----|----------|------|------|
| NET-1 | `struct socket` + `proto_ops` (stream/unix, inet stream, udp, icmp) in `net/socket.c`; socket syscalls dispatch through ops and move to `net/` | medium | L |
| NET-2 | `g_tcp_connect_busy` -> a sleeping lock / wait queue (red/green if it changes timing) | medium | S |
| NET-3 | Delete `net_stack.h`, the 3 unused and make the 10 owner-only private; replace the three globals with a config getter/setter | low | S |
| NET-4 | Move the in-function `extern` prototypes into `net/*.h` | low | S |

### 7.5 Proposed layout

```text
net/
  core/   net.c net_compat.c socket.c (new)
  ipv4/   ip.c icmp.c udp.c arp.c tcp.c tcp_input.c tcp_output.c tcp_cc.c tcp_listen.c tcp_internal.h
  sock/   sock_stream.c sock_udp.c sock_icmp.c sock_inet_ioctl.c
  apps/   dhcp.c dns.c
```

---

## 8. ipc

Files: `ipc/ipc.c` (wait queues, semaphores, ring buffers, channels),
`sysv_shm.c`, `posix_shm.c`, `early_unix.c`, `early_anonfd.c`,
`product_ipc.c`.

### 8.1 Public interface

`ipc.h`: 26 symbols, 10 used outside ipc (fs, process 1, boot 1; tests not
counted), 14 used only by ipc or tests, 2 unused (`semaphore_up`, `ipc_channel_find`). The public
ring-buffer functions are used only by tests. `sysv_shm.h` (4, syscalls),
`posix_shm.h` (4, 3 used by syscalls), `early_unix.h`, `early_anonfd.h`,
`product_ipc.h` (1, unused) belong to the early world (4.4).

### 8.2 Arch boundary

None.

### 8.3 Responsibility leaks

- Wait queues (`ipc/ipc.c:48-170`) are a scheduler primitive (X-7).
- `ipc/sysv_shm.c` allocates frames with `pmm_alloc_frame` /
  `pmm_frame_get` / `pmm_frame_put` directly (lane M2).
- Process exit calls `ipc_purge_waiters_for_process` by name (PRC-6).

### 8.4 Simplification candidates

| ID | Proposal | Risk | Size |
|----|----------|------|------|
| IPC-1 | Wait queues to `sched/wait.c` (X-7); semaphores on top of them or deleted if unused after that | high | M |
| IPC-2 | Ring buffer to `lib/` (or private to ipc) | low | S |
| IPC-3 | `sysv_shm` frames through `mm_alloc_frame` / page refcount API | low | S |
| IPC-4 | Early-world ipc files follow Q1 | - | - |

### 8.5 Proposed layout

```text
ipc/  channel.c sysv_shm.c posix_shm.c        (+ early/ only if Q1 keeps it)
```

---

## 9. drivers

### 9.1 Driver core (registry, bootstrap, resources)

Files: `kernel/driver_registry.c` (389), `kernel/resource_registry.c`,
`kernel/console_backend.c`, `kernel/input_events.c` (all driver-core code
living in `kernel/`), `drivers/init_drv.c` (274),
`drivers/driver_bootstrap.c`, `drivers/multilang_drivers.c`.

Public: `driver.h` 6 (`ir0_register_driver` 9 outside; 3 owner-only),
`driver_bootstrap.h` 3 (all owner-only, one file: lane V), `init_drv.h` 1,
`resource_registry.h` 9 (registration 15 outside; unregister/foreach only
tests and procfs), `driver_backend.h` (only the `IR0_LEGACY_PC_IO`
default), `platform_resource.h`, `platform_ops.h` 2.

Leaks: `drivers/init_drv.c:34-56` includes 10 private driver headers
(`drivers/input/ps2.h`, `drivers/storage/{ata,ahci,nvme}.h`,
`drivers/serial/serial.h`, `sound/{sound_blaster,adlib}.h`,
`drivers/dma/dma.h`, ...) and branches on `IR0_LEGACY_PC_IO` (`:78,86,94,172`):
a central file that knows every driver. `driver_bootstrap_register` exists
(11 calls) but only `init_drv.c` uses it.

### 9.2 tty / console / video / vga

Four layers for one output path: tty (`drivers/tty/console.c`, 1046),
console backend (`kernel/console_backend.c`), video console
(`drivers/video/console.c`, `console_renderer.c`, `typewriter.c`), VGA
text (`drivers/char/vga.c`).

| Header | Symbols | Notes |
|--------|--------:|-------|
| `console.h` | 31 | 6 `tty_*` + 25 `ir0_console_*`; `tty_input_char`, `tty_read_kernel`, `tty_write_kernel`, `tty_flush_input` owner-only; overlapping pairs `ir0_console_read`/`tty_read_kernel`, `ir0_console_flush_input`/`_session`/`tty_flush_input` (inventory 1.10) |
| `console_backend.h` | 14 | 5 used only by the KTM console mock and its host test (`console_backend_fb_scale`, `show_cursor`, `cursor_x`, `set_tty_serial_mirror`, `typewriter_init`) |
| `video_console.h` | 9 | driver-to-driver |
| `vga.h` | 10 | exports the global `cursor_pos` (`drivers/video/console_renderer.c` 65 uses); `console_puts` used only by the inline `print` |
| `typewriter.h` | 9 | 1 unused |
| `fb.h`, `vbe.h`, `video_backend.h` | 5 / 16 / 10 | `vbe.h` 11 owner-only + 4 unused |

Leaks: the x86 keyboard (`arch/x86-64/sources/interrupt/keyboard.c:179,321`)
feeds the tty directly (`ir0_console_keypress`, `stdin_wake_check`);
`drivers/tty/console.c` has 10 inline KTM/d1_16 diag hooks; signals and
process exit call tty internals (SIG-2, PRC-6); `IR0_LEGACY_PC_IO` in
`drivers/video/console.c` (5), `drivers/char/vga.c` (3), `vbe.c` (1).

### 9.3 input

`input.h` 14 (defined in `kernel/input_events.c` 9 and
`drivers/input/` 5), `input_backend.h` 18 (KTM-mockable),
`keyboard.h` 9 (implemented twice: `arch/x86-64/sources/interrupt/keyboard.c`
and `drivers/input/input_backend.c`), `ps2_mouse.h` 9, `ps2_mouse_pkt.h` 2,
`ps2_set1.h` 8 (scan-code tables used by the x86 keyboard). `irq.h`
publishes `irq_keyboard_init` and `irq_keyboard_poll_ps2`: a device in the
interrupt facade. `drivers/input/input_backend.c` has 7
`IR0_LEGACY_PC_IO` blocks.

### 9.4 block / storage

`block/blockdev.c` + `blockdev.h` (18 symbols): two addressing modes for
the same operations, by handle (`ir0_block_read`, `_write`, `_is_present`,
`_get_info`) and by name (`ir0_block_read_by_name` 33 calls,
`_write_by_name`, `_name_is_present` 23, `_sector_count_by_name`,
`ir0_block_legacy_name` 14), plus index iteration (`_count`, `_name_at`,
`_dev_at`). `partition.h` 6 (2 unused). `ahci_api.h`: the x86 arch calls
`ahci_map_mmio_in_directory` (arch -> driver). `drivers/storage/ata.c:23`
includes `fs/vfs.h`.

### 9.5 net drivers, serial, Bluetooth, sound, virtio, USB

- `drivers/net/bcmgenet.c:17` includes `arch/arm64/include/arm64_board.h`
  (driver -> arch-private board header); `net_drivers.c` uses
  `IR0_LEGACY_PC_IO`; `dma.h` (`dma_alloc`) is used only by net drivers.
  rtl8139 and the PIT/keyboard/mouse/SB16 are dispatched by hard-coded
  calls in `arch/x86-64/sources/interrupt/isr_handlers.c:447-475`; only
  `bcmgenet` uses `irq_request` (X-8).
- `serial_io.h` (7): x86 `drivers/serial/serial.c`, arm64
  `arch/arm64/sources/serial.c`; `serial_read_char` unused; KTM calls
  `serial_print` 24 times directly.
- `bluetooth.h` (14, 1 unused): fs calls 11 readers; idle polls it.
- `sound/`: `sound_blaster.h` 13 (5 unused, 5 owner-only);
  `sound/sound_blaster.c:24` includes `drivers/timer/pit/pit.h`, `:22`
  includes `drivers/dma/dma.h` (ISA 8237 DMA, same basename as `ir0/dma.h`);
  `sb16_irq_handler` is called from the x86 ISR.
- `virtio_9p.h` 16 (1 unused) used by `fs/hostshare_9p.c`;
  `virtio_mmio.h` 6, arm64 only, `arm64_virtio_mmio_*` names in a
  public header.
- `usb_host.h` 3 (2 owner-only).
- `multilang_drivers.[ch]`: example drivers (inventory: delete if
  example-only).

### 9.6 Simplification candidates

| ID | Proposal | Risk | Size |
|----|----------|------|------|
| DRV-1 | Move `kernel/{driver_registry,resource_registry,console_backend,input_events}.c` into `drivers/base/` (and `drivers/tty`, `drivers/input`) | low | S |
| DRV-2 | Self-registering drivers (`driver_bootstrap_register` from each driver, or a linker initcall section, Q13); `init_drv.c` stops including driver internals | medium | M |
| DRV-3 | Console stack: one tty API (`tty_*`), one console-backend contract; delete the 5 backend calls only the mock uses and the read/flush duplicates; `cursor_pos` private (Q8) | medium | L |
| DRV-4 | x86 i8042 keyboard to `drivers/input/` (port I/O through the facade); remove `irq_keyboard_*` from `irq.h`; one `keyboard.h` implementation per platform file | medium | M |
| DRV-5 | Block: one handle-based API; by-name only as `ir0_block_lookup_by_name()`; delete `_by_name` variants and `legacy_name` after callers convert | medium | M |
| DRV-6 | `ahci_map_mmio_in_directory` driven by the driver through mm (`ioremap`-style), not by arch | low | S |
| DRV-7 | Board resources for `bcmgenet` through `platform_resource.h` / FDT, not `arm64_board.h` | low | S |
| DRV-8 | All IRQ users through `irq_request()`; the x86 ISR calls `irq_dispatch()` like arm64 (X-8) | medium | M |
| DRV-9 | Delete `multilang_drivers` if example-only; delete unused SB16/VBE/partition/serial/typewriter entry points (Appendix B) | low | S |

### 9.7 Proposed layout

```text
drivers/
  base/        driver_registry.c resource_registry.c driver_bootstrap.c init_drv.c
  tty/         console.c (tty) console_backend.c stdin.c (from kernel/syscalls.c) serial/ (8250, pl011)
  video/       console*.c typewriter.c fb.c vbe.c video_backend.c fonts/ vga.c
  input/       input.c input_events.c input_backend.c ps2*.c i8042_kbd.c
  block/       blockdev.c (from block/) storage/ (ata, ahci, nvme, partition)
  net/ virtio/ bluetooth/ usb/
  clocksource/ (pit, hpet, lapic, arm_arch_timer)   irqchip/ (gic_v2, bcm2836)   rtc/ (cmos, pl031)   (Q4)
  acpi/        acpi_pm.c find_hpet.c
  sound/       sound_blaster.c adlib.c isa_dma.c
```

---

## 10. time / clock

Files: `drivers/timer/clock_system.c` (416, the clock core),
`drivers/timer/{pit,hpet,lapic,rtc,acpi}/`, `kernel/time/clock_wait.c`,
`kernel/time/clock_accounting.c`, `kernel/vdso.c`,
`kernel/syscalls/time_syscalls.c`, `drivers/rtc/rtc_calendar.c`,
`arch/arm64/sources/{timer,rtc}.c`, `arch/*/sources` `timer_read`.

### 10.1 Public interface

`clock.h` 20 (all used outside: uptime ms 74, tick count 11 including 4
in tests, realtime 14, ...); `clock_wait.h` 4 (`ir0_clock_wait_arm`, `_fire_due` owner-only);
`rtc.h` 8 (`rtc_read_register`, `rtc_bcd_to_binary`,
`rtc_apply_cmos_format` are CMOS details; 5 owner-only); `delay.h`
`udelay`; `early_clock.h` 2 (ktm only); `vdso.h` 4; `time.h` types;
`abi/vdso_contract.h`. Private `drivers/timer/*/*.h`: 6 unused
(`lapic_init_timer`, `lapic_available`, `lapic_send_eoi`,
`hpet_set_address`, `find_hpet_table`; `pit.h` used by sound and the x86
ISR).

### 10.2 Arch boundary

The generic clock core knows the x86 timers: `clock_state` fields
`pit_*`, `hpet_*`, `lapic_*` (`drivers/timer/clock_system.c:45-54`),
`#if !IR0_LEGACY_PC_IO` (`:179`, `:200`), an `extern void init_PIT()`
inside a function (`:185`) and `#include "pit/pit.h"` (`:29`). arm64
drives `clock_tick()` from `arch/arm64/sources/timer.c`; x86 from
`drivers/timer/pit/pit.c:45` (also an in-function `extern`).
`cpu_info.h` declares `timer_init/set_frequency/get_frequency` (arm64 only,
two unused). `lapic.c:35,51` uses `cpuid`/`rdmsr` (fine for an x86 driver,
but see X-4).

### 10.3 Responsibility leaks

- The clock core lives in `drivers/` while its consumers and helpers live in
  `kernel/time/`.
- `drivers/timer/acpi/acpi_pm.c` is ACPI power-off/suspend, not a timer.
- `drivers/rtc/rtc_calendar.c` is pure calendar math (no device).
- `pit.c` description says "PIT and PIC initialization": the PIC is also in
  `arch/x86-64/sources/interrupt/pic.c` (check for overlap; uncertain).

### 10.4 Simplification candidates

| ID | Proposal | Risk | Size |
|----|----------|------|------|
| TIM-1 | Clock core to `kernel/time/clock.c`; timers register as clock-event/clock-source devices (Linux `clockevents_register_device`), so the core has no PIT/HPET/LAPIC fields and no `#if` | medium | M |
| TIM-2 | `acpi_pm.c` to `drivers/acpi/` | low | S |
| TIM-3 | CMOS-only functions private to the CMOS driver; `rtc.h` keeps `rtc_read_time` + the calendar conversions; `rtc_calendar.c` to `lib/` or `kernel/time/` | low | S |
| TIM-4 | `clock_wait.h` owner-only functions private; finish lane T2 | low | S |
| TIM-5 | Delete the 6 unused timer-driver functions | low | S |
| TIM-6 | `time_syscalls.c` and `vdso.c` into `kernel/time/` | low | S |

### 10.5 Proposed layout

```text
kernel/time/   clock.c clock_accounting.c clock_wait.c vdso.c syscalls.c calendar.c
drivers/clocksource/  pit.c hpet.c lapic.c (+ arm arch timer if Q4 says drivers/)
drivers/rtc/          cmos.c (+ pl031.c)
```

---

## 11. boot

Files: `kernel/kernel_wiring.c` (24, `kmain` -> `ir0_kernel_boot`),
`kernel/boot_flow.c` (27, `ir0_kernel_boot` -> phases),
`kernel/boot_init.c` (361, the phases), `kernel/init_handoff.c`,
`kernel/boot_params.c`, `kernel/cmdline.c`, `kernel/rootfs_base.c`,
`kernel/version_stamp.c`, `kernel/ash_smoke.c`, `kernel/cpu_topology.c`,
`kernel/utsname_info.c`, `kernel/boot_firmware_sysfs.c`,
`kernel/boot_log_hostshare.c`, `arch/common/{boot_arch,boot_port,boot_log}.c`.

### 11.1 Public interface

12 boot headers: `boot_arch.h` (2 weak arch hooks), `boot_port.h` (6 weak
arch hooks), `boot_params.h` 2, `boot_flow.h` 1 (owner-only),
`boot_log.h` 8 (arch-common implementation), `boot_log_hostshare.h` 1
(owner-only), `boot_memory.h` (mm), `boot_firmware_sysfs.h`, `kernel.h`
(`kmain`), `init_handoff.h` 1 (owner-only), `cmdline.h` 2,
`multiboot.h`. Plus `arch_info.h` `early_init`, `syscall_init`,
`boot_entry_name` (and 7 never-defined prototypes, 0.6), `ash_smoke.h` 5,
`utsname_info.h`, `version.h`, private `kernel/boot_init.h` (6 phases,
owner-only), `kernel/rootfs_base.h` (included by
`arch/arm64/sources/boot_port.c:28`).

### 11.2 Arch boundary

Boot hooks are spread over `boot_arch.h`, `boot_port.h`, `arch_info.h`
and `oops_port.h`, with weak defaults in `arch/common/` and strong
definitions in `arch/*/sources/`. `arch/common/` holds generic code
(`boot_log.c` is a portable logger) inside the arch tree.

### 11.3 Responsibility leaks

- The arm64 boot port drives the early rootfs and bpivot directly
  (`arch/arm64/sources/boot_port.c:60-77`): rootfs policy in an arch file.
- `ash_smoke` (a smoke-test hook) is called from syscalls (3), tty (1) and
  the x86 arch (`ir0_cmdline_ash_smoke_enabled` 5): test hooks in product
  paths (KTM territory).

### 11.4 Simplification candidates

| ID | Proposal | Risk | Size |
|----|----------|------|------|
| BOOT-1 | One `init/main.c` (`kmain` with the phase sequence) replacing `kernel_wiring.c`, `boot_flow.c` and the dispatch part of `boot_init.c` | low | S |
| BOOT-2 | One arch boot-hook header (`boot_port.h` + `boot_arch.h` + the live part of `arch_info.h`); `boot_flow.h`, `init_handoff.h`, `boot_log_hostshare.h` private to init | low | S |
| BOOT-3 | Move `arch/common/*.c` to the generic owners; replace weak defaults by required per-ISA hooks (fails at link time instead of silently using a default) | low | S |
| BOOT-4 | Rootfs selection in generic init, arm64 only reports the block device (follows Q1) | medium | S |
| BOOT-5 | `ash_smoke` hooks behind KTM | low | S |

### 11.5 Proposed layout

```text
init/   main.c boot_init.c init_handoff.c cmdline.c boot_params.c version_stamp.c utsname_info.c rootfs_base.c boot_log.c boot_log_hostshare.c boot_firmware_sysfs.c cpu_topology.c
```

---

## 12. KTM

Files: `ktm/` (26 `.c`, 4358 lines), `ktm/include/` (14 headers),
`ktm/mocks/` (7 backends), `includes/ir0/ktm/` (15 headers).

### 12.1 Public interface

`includes/ir0/ktm/*`: `klog.h` (re-export, 74 includers), `fault.h`
(`KTM_FAULT_HIT` 15 sites), `checkpoint.h` (`KTM_CHECKPOINT` 10),
`event.h` (`ktm_event_emit4` 13), `stack_watch.h` 6 (x86 ISR + procfs),
`user_canary.h` 3 (process, mm, syscall dispatch), `deferred.h`,
`userdev.h`, `probe.h`, `scenario.h`, `snapshot.h`, `transport.h`,
`assert.h`, `uapi.h`, `ktm.h`. Unused: 3 in `assert.h`, `ktm_checkpoint_emit`,
`ktm_write_cstr`, `ktm_suite_pass/fail_count`, `ktm_snapshot_diff`,
`KTM_DUMP_ARG`; in `ktm/include/ktm.h` 4 functions and 14 macros.
`ktm/include/d1_12_read_diag.h`, `d1_13_malloc_pf_diag.h`,
`d1_16_tty_read_diag.h`, `ktm_probe_diag.h` are inline hooks included by
syscall dispatch, the tty and the x86 page-fault path.

### 12.2 Responsibility leaks

- **The kernel logger lives in KTM.** `ktm/klog.c` (629 lines) and
  `ktm/include/klog.h` (32 symbols: `klog_*`, `kprintf`) are the core
  logging service for the whole kernel (`klog_print` ~270 refs,
  `klog_smoke` ~105, `klog_*_fmt` ~170), reached through
  `ir0/klog.h` -> `ir0/ktm/klog.h` -> `<klog.h>`. KTM should be a consumer.
- Inline KTM/diag in policy and critical code: `fs/pipe.c` 16,
  `kernel/process/fork.c` 12, `drivers/tty/console.c` 10,
  `kernel/syscalls/syscall_dispatch.c` 9, `kernel/syscalls/mm_syscalls.c` 4,
  `io_syscalls.c` 4 (lanes D1/D2/D4).
- Bug-ticket-named probe files (`d1_12`, `d1_13`, `d1_16`) with hooks in
  product code.

### 12.3 Simplification candidates

| ID | Proposal | Risk | Size |
|----|----------|------|------|
| KTM-1 | Move `klog` to `kernel/printk/` (or `lib/`, Q7) with one public header `ir0/klog.h`; KTM consumes it | low | M |
| KTM-2 | Subsystem diag hooks for the six files above (lanes D1/D2/D4, plus pipe and tty) | low | M |
| KTM-3 | Retire or generalize the `d1_*` probes into trace events | low | S |
| KTM-4 | Collapse `includes/ir0/ktm/` to the hooks product code calls (`KTM_FAULT_HIT`, `KTM_CHECKPOINT`, event, user canary); the rest private to `ktm/`; delete the unused | low | S |

### 12.4 Proposed layout

```text
ktm/  core/ (registry, scenario, assert, transport) probes/ (probe_diag, d1_*) fault/ event/ mocks/ include/
```

---

## 13. lib

Files: `includes/string.c` (a source file in the header directory),
`includes/string.h`, `includes/stdint.h`, `includes/stddef.h`,
`includes/errno.h`, `kernel/errno.c` + `kernel/errno.h` (`strerror`, used by
tests only), `includes/ir0/errno.h` (re-export), `includes/ir0/overflow.h`,
`includes/ir0/types.h` (2 unused macros), `includes/ir0/compat.h` and
`interop.h` (C++/Rust glue, used by `cpp/` and `rust/`; uncertain beyond
grep).

Leaks and candidates:

| ID | Proposal | Risk | Size |
|----|----------|------|------|
| LIB-1 | `includes/string.c` -> `lib/string.c` | low | S |
| LIB-2 | One `errno.h`; `strerror` to `lib/` (test-only today: keep only if wanted) | low | S |
| LIB-3 | Pure helpers to `lib/`: ring buffer (ipc), calendar (`rtc_calendar.c`), `elf64_image.c` parser (if Q6 says so) | low | S |
| LIB-4 | `LIB_OBJS` split by owner; fix `subsystems.json` (X-9) | low | S |

---

## 14. Arch facades (`includes/ir0/arch_*.h`, `asm/*`)

### 14.1 Inventory

| Facade | Symbols | x86 / arm64 impl | Outside users | Notes |
|--------|--------:|------------------|---------------|-------|
| `cpu.h` | 22 | 12 / 12 (+ cpu masks in `kernel/cpu_topology.c`) | `irq_save/restore` ~150, `smp_mb` 13, `cpu_idle` 4 | x86-only `rdmsr/wrmsr/cpuid/debug_reg_*` in the portable facade (0.5); `cpu_online_count`, `cpu_is_present/online/active` used only by a host test |
| `arch_io.h` | 15 | 9 / 15 | port I/O in drivers ~210 | arm64-only `mmio_*` (unused); arm64 port I/O stubs return 0 |
| `arch_mm.h` | 34 | 29 / 29 | mm, process, syscalls | PTE encoding leaks out of mm (5.2) |
| `mm.h`, `tlb.h`, `page_fault.h`, `cacheflush.h`, `dma.h` | 5/4/5/1/5 | both | | `page_fault.h` 2 never defined + `get_fault_address` unused |
| `context.h`, `context_backend.h` | 3/3 | both | sched | clean |
| `task.h`, `arch_task.h`, `task_ops.h` | 6/16/9 | inline / both | process | x86 segments (R1); 5+2 unused; `task.h` 5 unused macros |
| `syscall_frame.h`, `syscall_id.h`, `entry.h` | 3/1/1 | both | | `rip_hw` (R3) |
| `signal_irq.h`, `fpu.h`, `fpu_backend.h`, `tls.h`, `fork.h`, `vdso.h` | 12/1/7/6/3/4 | both | | `tls.h` 3 unused |
| `irq.h` | 9 | both | | `irq_keyboard_*` device hooks (DRV-4) |
| `cpu_info.h` | 15 | 10 / 13 | procfs 9 | x86-shaped (CPUID leaf, EDX/ECX bits, clflush); 2 never defined, 2 unused |
| `arch_info.h` | 12 | 5 / 5 | boot 3, fs 2, syscalls 1 | 7 never defined |
| `arch_elf.h`, `arch_debug.h`, `arch_pf_debug.h`, `delay.h`, `early_clock.h`, `platform_ops.h`, `power_manag.h` | small | both | | `power_manag.h`: `system_halt/reboot/poweroff` unused |
| `serial_io.h`, `rtc.h`, `acpi_pm.h`, `keyboard.h` | | arch on one ISA, driver on the other | | 0.7 |
| `percpu.h` | 7 | generic (`kernel/percpu.c`) | | |
| Aggregators `arch_cpu.h`, `arch_port.h`, `arch_types.h` | 0 | | 19 / 37 / 3 | 0.3 |
| ISA-specific headers in `includes/ir0/`: `arch_syscall_frame_{x86_64,arm64}.h`, `arch_task_context_{x86_64,arm64}.h`, `sigcontext_{x86_64,arm64}.h`, `syscall_{x86_64,arm64}.h`, `asm_offsets.h` | | | each included by its ISA's `asm/` shim only | belong under `arch/<isa>/include/asm/` |

`arch/<isa>/include/asm/` (13 headers per ISA, matching names:
`cacheflush`, `fcntl`, `sigcontext`, `signal_contract`, `stat`,
`syscall_frame`, `syscall_user`, `task_access`, `task_context`,
`vdso_contract`, plus x86 `context`, `debug_trap`,
`process_ctx_invariant`; arm64 `boot`, `esr`, `exc_frame`) is the right
mechanism; `includes/ir0/*.h` facades include `<asm/x.h>` and the build
selects the ISA directory. x86 also keeps `arch/x86-64/include/interrupt/`
(`idt.h`, `io.h`, `keyboard.h`, `pic.h`; 5 unused in `keyboard.h`, 1 in
`pic.h`).

### 14.2 Simplification candidates

| ID | Proposal | Risk | Size |
|----|----------|------|------|
| ARCH-1 | Split `cpu.h`/`arch_io.h`: portable part stays; MSR/CPUID/debug registers/port I/O to x86 `asm/` headers; delete arm64 stubs (X-4) | medium | M |
| ARCH-2 | Delete the aggregators and one-line arch/common headers; each file includes what it uses (X-3) | low | M (many includers) |
| ARCH-3 | Move the 9 ISA-specific headers from `includes/ir0/` to `arch/<isa>/include/asm/` | low | S |
| ARCH-4 | Delete the 15 never-defined prototypes and the unused facade entries (Appendix B) | low | S |
| ARCH-5 | `/proc/cpuinfo` through one arch hook (`arch_show_cpuinfo(buf)`, Linux `show_cpuinfo`); shrink `cpu_info.h` to neutral queries | low | M |
| ARCH-6 | `fault.h`: drop the x86-only kinds (`STACK_SEGMENT`, `SEGMENT_NOT_PRESENT`, `DOUBLE`) into `PROTECTION`/`OTHER` and `code_seg`/`stack_seg` into the arch `show_regs` hook (R5) | low | S |
| ARCH-7 | Rename legacy `arch_*` facades to Linux-like topic names when touched (Q5) | low | M |

---

## 15. Prioritized summary

P0 = cheap, low risk, unblocks other work; P1 = structural, do next;
P2 = after P1 or after a decision. "Q" marks items that wait for a
maintainer decision.

| Pri | ID | Subsystem | Change | Risk | Size |
|-----|----|-----------|--------|------|------|
| P0 | X-3 / ARCH-2 | all | Delete re-export/aggregator headers (`net_stack.h`, `arch_cpu.h`, `arch_port.h`, arch/common one-liners, reverse wrappers, empty headers) | low | M |
| P0 | X-5 / ARCH-4 | arch | Delete 15 never-defined prototypes and unused facade entries | low | S |
| P0 | X-1 | drivers/time | Replace `IR0_LEGACY_PC_IO` (27 sites, 8 files) by per-platform files; add it to the ISA-conditional guard | medium | M |
| P0 | PRC-3 | process/fs | One credential/permission API (delete `is_root`, `get_current_uid/gid`, two path-based checks; merge 3 headers) | low | S |
| P0 | PRC-8 | process | Delete `kernel/process.c` stub | low | S |
| P0 | FS-2 | fs | devfs/procfs/sysfs/pseudo_fs handlers static; delete 16 unused | low | M |
| P0 | FS-4 | fs | Delete minix shell UI + 12 unused minix functions | low | S |
| P0 | SYS-6 | syscalls | Userspace syscall wrappers and inline-asm stubs out of `includes/ir0/` | low | S |
| P0 | NET-3 / NET-4 | net | Delete `net_stack.h` and unused; globals behind a getter; in-function `extern`s to headers | low | S |
| P0 | MM-2 | mm | Finish lanes M1-M4 | low | S |
| P0 | MM-4 | mm/net/ktm | Atomics through the facade *(in flight)* | low | S |
| P1 | X-7 / SCH-5 / IPC-1 | sched/ipc | One wait-queue primitive in `sched/wait.c`; convert the 14 open-coded sleeps | high | L |
| P1 | X-8 / DRV-8 | arch/drivers | All IRQ users via `irq_request()`; x86 ISR calls `irq_dispatch()` | medium | M |
| P1 | PRC-1 | process | Shrink `kernel/process.h`; real `ir0/process.h` facade | medium | L |
| P1 | PRC-4 / MM-1 / MM-6 | mm/process/syscalls | VMA, mmap/brk, page walks and `mm_struct` into `mm/`; PTE encoding mm-private | medium | L |
| P1 | PRC-5 | process/fs | One fd-table owner (lane F2) | medium | M |
| P1 | PRC-6 / SIG-2 | process/signals/tty/net | Exit and signals stop calling tty/ipc/tcp internals | medium | M |
| P1 | SYS-2 / SYS-3 / SYS-5 | syscalls | Clean `syscall_dispatch()`: trace hooks, fork wake and segment restore out, no `#if` | low-medium | S |
| P1 | SCH-1 / SCH-2 | sched | Idle stops polling drivers; `switch_mm()` hook instead of lazy root fill | low-medium | S |
| P1 | TIM-1 | time | Clock core to `kernel/time/` with clock-event registration; no x86 timers in the core | medium | M |
| P1 | NET-1 | net | `struct socket` + `proto_ops`; socket syscalls into net | medium | L |
| P1 | NET-2 | net | Replace the `g_tcp_connect_busy` spinner | medium | S |
| P1 | FS-3 | fs/net | Net config/ping owned by net | medium | M |
| P1 | KTM-1 | ktm | klog out of `ktm/` | low | M |
| P1 | ARCH-1 / X-4 | arch | x86 primitives out of portable facades; no arm64 stubs | medium | M |
| P1 | ARCH-3 | arch | ISA-specific headers into `arch/<isa>/include/asm/` | low | S |
| P1 | ARCH-5 | arch/fs | `/proc/cpuinfo` via arch hook | low | M |
| P1 | DRV-1 / DRV-2 | drivers | Driver core into `drivers/base/`; self-registering drivers | medium | M |
| P1 | DRV-4 / DRV-5 | drivers | x86 keyboard to `drivers/input`; one block API | medium | M |
| P1 | SYS-7 | syscalls/tty | tty stdin code out of `kernel/syscalls.c` | low | S |
| P1 | BOOT-1 / BOOT-2 | boot | One `init/main.c`; one arch boot-hook header | low | S |
| P1 | Q1 / SYS-8 | early world | Decide the early rootfs world's fate | high | XL |
| P2 | FS-1 (Q3) | fs | Pseudo filesystems as real VFS mounts; retire path routing and `named_*` tables | high | XL |
| P2 | SYS-1 (Q2) | syscalls | Syscall implementations to their owners | medium | XL |
| P2 | X-6 (Q4) | arch/drivers | One placement policy for platform device drivers | medium | L |
| P2 | X-2 | build | Drop private `-I` roots; rename colliding basenames | low | M |
| P2 | DRV-3 (Q8) | drivers | Console stack consolidation | medium | L |
| P2 | SIG-3 | signals | Remove per-syscall sigframe abandonment | high | M |
| P2 | SIG-1 / SIG-4 / SIG-5 | signals | Lane G1, `signal_irq.h` cleanup, signal syscalls next to signals | low | M |
| P2 | MM-3 / MM-5 | mm | Owner-only headers private; one page-flag namespace | low | M |
| P2 | FS-5..FS-8, FS-6 | fs | diag hook, one VFS facade, drivers register pseudo nodes | low-medium | M |
| P2 | TIM-2..TIM-6 | time | acpi_pm, rtc.h, clock_wait, dead timer code, layout | low | S |
| P2 | KTM-2..KTM-4 | ktm | Diag hooks; d1 probes; header collapse | low | M |
| P2 | BOOT-3..BOOT-5 | boot | arch/common out; rootfs policy generic; ash_smoke behind KTM | low | S |
| P2 | LIB-1..LIB-4, X-9 | lib/build | `lib/` directory; errno; object lists and manifest | low | S |
| P2 | SYS-4 / SYS-9 | syscalls | One table mechanism; console syscalls to ioctls | low-medium | M |
| P2 | DRV-6 / DRV-7 / DRV-9 | drivers | ahci mapping, bcmgenet board header, dead driver code | low | S |
| P2 | ARCH-6 / ARCH-7 | arch | Neutral fault kinds; facade names | low | M |

Suggested first wave (disjoint files, no concurrent-branch conflict):
X-5, PRC-3, PRC-8, FS-4, SYS-6, NET-3/4, ARCH-3, BOOT-1. Then X-1 and
FS-2 (larger but mechanical), then X-7 as the first core-first item.

---

## 16. Questions for the maintainer

Q1. **Early rootfs world** (`erfs*`, `early_*`, `product_*`, `ramtab`,
`bpath`/`bpivot`, ~12k lines and ~20 headers). Options: (a) retire it once
the common VFS/process path boots both ISAs from the block image (arm64
already enters userspace through `kexecve("/sbin/init")`); (b) freeze it
behind one header as a boot-only module; (c) fold the useful part into
`fs/` as an initramfs filesystem mounted by the common VFS.

Q2. **Where syscall implementations live.** (a) Linux: each in its owner
(`fs/`, `mm/`, `net/`, `kernel/signal/`, ...) with only entry/dispatch in
`kernel/entry/`; (b) keep `kernel/syscalls/` but split by owner
subdirectory and restrict it to owner facades (no private headers).

Q3. **Pseudo filesystems.** (a) full `vfs_fstype` mounts with inode/file
ops (Linux); (b) keep the `pseudo_fs` registry but resolve it inside VFS
lookup so syscalls never test path prefixes. Same question for the
`named_*` side tables (inodes in the parent fs vs a VFS-owned table).

Q4. **Platform device drivers** (serial, RTC, timers, IRQ controllers,
keyboard, early virtio-blk). (a) `drivers/<class>/` for both ISAs, `arch/`
only CPU/MMU/entry (Linux); (b) SoC/board drivers under `arch/<isa>/` and
PC-legacy drivers under `drivers/`; (c) status quo with a documented rule.

Q5. **Facade naming.** Keep `includes/ir0/arch_*.h` (legacy prefix) or
move to Linux topic names (`asm/io.h`, `asm/pgtable.h`, `asm/tlbflush.h`,
`asm/processor.h`) with `includes/ir0/` holding only portable APIs?

Q6. **Exec/ELF placement.** `kernel/process/exec/` or Linux `fs/exec.c` +
`fs/binfmt_elf.c`?

Q7. **Logger home.** `kernel/printk/` or `lib/`; and confirm lane L1
(retire `logging.h` aliases) before moving it.

Q8. **Console stack.** How many layers stay: tty, console backend, video
console, VGA text, typewriter. Is the typewriter effect a product feature
or can it go?

Q9. **mm_struct and VMAs** owned by `mm/` (Linux) or by process?

Q10. **Header names.** `includes/ir0/wait.h` holds wait-status macros
(`WIFEXITED`); Linux `linux/wait.h` is wait queues. Rename the status
header (`wait_status.h`) to free `wait.h` for X-7?

Q11. **`kernel/` top level** (being reorganized by another branch): does
the target match `kernel/{entry,signal,process,time,irq,panic,printk}` +
`init/` + `lib/` as proposed here?

Q12. **Block device identity.** Keep names (`"vda"`) as the identity in the
API, or handles only with one name lookup?

Q13. **Driver initialization.** Linker initcall sections (Linux) or the
explicit `driver_bootstrap_register` table called from each driver?

Q14. **IR0-specific syscalls** (`console_scroll`, `console_clear`,
`keymap_set/get`): keep as syscalls, or move to tty ioctls so the syscall
table is Linux-only?

Q15. **Weak symbols.** `arch/common/*.c` provides weak defaults for arch
hooks. Keep (convenient for new ISAs) or require every ISA to define every
hook (link error instead of a silent default)?

---

## Appendix A. Per-header numbers (`includes/ir0/`)

"Defined in" is the subsystem that defines most of the header's symbols
(`-` = macros, types or inlines only). "Owner/tests only" counts symbols
used only by the defining subsystem or by tests; for `-` headers it counts
symbols used only inside the header or by tests.

| Header | Defined in | Symbols | Used outside owner | Owner/tests only | Unused | Includers |
|--------|-----------|--------:|-------------------:|-----------------:|-------:|----------:|
| `abi/brk_contract.h` | - | 2 | 0 | 2 | 0 | 2 |
| `abi/elf_interp_contract.h` | - | 3 | 3 | 0 | 0 | 2 |
| `abi/mmap_contract.h` | - | 15 | 3 | 10 | 2 | 10 |
| `abi/musl_cred_abi.h` | - | 0 | 0 | 0 | 0 | 1 |
| `abi/signal_contract.h` | - | 0 | 0 | 0 | 0 | 2 |
| `abi/vdso_contract.h` | - | 2 | 2 | 0 | 0 | 1 |
| `acpi_pm.h` | arch/arm64/time | 4 | 1 | 2 | 1 | 5 |
| `acpi_rsdp.h` | - | 1 | 1 | 0 | 0 | 5 |
| `ahci_api.h` | drivers/storage | 1 | 1 | 0 | 0 | 2 |
| `arch_cpu.h` | - | 0 | 0 | 0 | 0 | 19 |
| `arch_debug.h` | arch (both) | 1 | 1 | 0 | 0 | 3 |
| `arch_elf.h` | arch (both) | 3 | 3 | 0 | 0 | 4 |
| `arch_info.h` | arch (both) | 12 | 5 | 0 | 7 | 2 |
| `arch_io.h` | arch (both) | 15 | 7 | 2 | 6 | 14 |
| `arch_mm.h` | arch (both) | 34 | 33 | 1 | 0 | 21 |
| `arch_pf_debug.h` | arch (both) | 2 | 2 | 0 | 0 | 3 |
| `arch_port.h` | - | 0 | 0 | 0 | 0 | 37 |
| `arch_syscall_frame_arm64.h` | - | 11 | 11 | 0 | 0 | 1 |
| `arch_syscall_frame_x86_64.h` | - | 11 | 11 | 0 | 0 | 1 |
| `arch_task.h` | - | 16 | 11 | 0 | 5 | 10 |
| `arch_task_context_arm64.h` | - | 2 | 2 | 0 | 0 | 1 |
| `arch_task_context_x86_64.h` | - | 2 | 2 | 0 | 0 | 1 |
| `arch_types.h` | - | 0 | 0 | 0 | 0 | 3 |
| `ash_smoke.h` | boot | 5 | 5 | 0 | 0 | 5 |
| `asm_offsets.h` | - | 0 | 0 | 0 | 0 | 1 |
| `audio_backend.h` | ktm/sound | 6 | 6 | 0 | 0 | 5 |
| `blockdev.h` | block | 18 | 14 | 4 | 0 | 25 |
| `bluetooth.h` | drivers/bluetooth | 14 | 13 | 0 | 1 | 7 |
| `boot_arch.h` | - | 0 | 0 | 0 | 0 | 3 |
| `boot_firmware_sysfs.h` | - | 0 | 0 | 0 | 0 | 5 |
| `boot_flow.h` | boot | 1 | 0 | 1 | 0 | 2 |
| `boot_log.h` | - | 0 | 0 | 0 | 0 | 9 |
| `boot_log_hostshare.h` | boot | 1 | 0 | 1 | 0 | 2 |
| `boot_memory.h` | mm | 11 | 5 | 5 | 1 | 20 |
| `boot_params.h` | boot | 2 | 1 | 1 | 0 | 11 |
| `boot_port.h` | - | 0 | 0 | 0 | 0 | 6 |
| `bpath.h` | syscalls/early/fs | 7 | 3 | 3 | 1 | 7 |
| `bpivot.h` | fs/tests | 13 | 6 | 6 | 1 | 8 |
| `cacheflush.h` | arch (both) | 1 | 1 | 0 | 0 | 3 |
| `chmod.h` | fs | 2 | 1 | 0 | 1 | 2 |
| `clock.h` | time/tests | 20 | 20 | 0 | 0 | 43 |
| `clock_wait.h` | time | 4 | 2 | 2 | 0 | 7 |
| `clone.h` | - | 0 | 0 | 0 | 0 | 5 |
| `cmdline.h` | boot | 2 | 1 | 1 | 0 | 4 |
| `compat.h` | - | 4 | 2 | 0 | 2 | 0 |
| `console.h` | drivers/tty | 31 | 25 | 6 | 0 | 16 |
| `console_backend.h` | drivers/core/ktm | 14 | 9 | 5 | 0 | 13 |
| `context.h` | arch (both) | 3 | 2 | 1 | 0 | 5 |
| `context_backend.h` | arch (both) | 3 | 3 | 0 | 0 | 5 |
| `copy_user.h` | mm | 10 | 7 | 0 | 3 | 39 |
| `cpu.h` | arch (both) | 22 | 11 | 11 | 0 | 39 |
| `cpu_info.h` | arch (both) | 15 | 11 | 0 | 4 | 4 |
| `cred_transition.h` | - | 3 | 2 | 0 | 1 | 2 |
| `credentials.h` | process | 5 | 4 | 0 | 1 | 17 |
| `cwd.h` | process | 2 | 1 | 0 | 1 | 3 |
| `debug_runtime.h` | - | 3 | 0 | 0 | 3 | 1 |
| `delay.h` | arch (both) | 1 | 1 | 0 | 0 | 4 |
| `devfs.h` | fs/syscalls | 53 | 19 | 33 | 1 | 12 |
| `dma.h` | arch (both) | 5 | 5 | 0 | 0 | 4 |
| `driver.h` | drivers/core | 6 | 3 | 3 | 0 | 16 |
| `driver_backend.h` | - | 0 | 0 | 0 | 0 | 7 |
| `driver_bootstrap.h` | drivers/core | 3 | 0 | 3 | 0 | 2 |
| `early_anonfd.h` | ipc | 12 | 9 | 3 | 0 | 3 |
| `early_clock.h` | arch (both) | 2 | 2 | 0 | 0 | 3 |
| `early_cpio.h` | - | 7 | 3 | 3 | 1 | 3 |
| `early_elf_stack.h` | boot | 1 | 0 | 1 | 0 | 2 |
| `early_io.h` | syscalls/early | 2 | 0 | 0 | 2 | 1 |
| `early_mm.h` | syscalls/early | 2 | 0 | 2 | 0 | 2 |
| `early_process.h` | syscalls/early | 31 | 0 | 15 | 16 | 6 |
| `early_rootfs.h` | syscalls/early | 172 | 10 | 142 | 20 | 13 |
| `early_syscall.h` | syscalls/early | 2 | 0 | 1 | 1 | 9 |
| `early_time.h` | syscalls/early | 3 | 0 | 2 | 1 | 3 |
| `early_unix.h` | ipc | 9 | 9 | 0 | 0 | 2 |
| `early_vfs.h` | syscalls/early | 2 | 0 | 0 | 2 | 1 |
| `elf64_image.h` | process | 2 | 1 | 1 | 0 | 4 |
| `elf_loader.h` | process | 5 | 2 | 0 | 3 | 3 |
| `entry.h` | syscalls | 1 | 1 | 0 | 0 | 3 |
| `epoll.h` | syscalls | 3 | 2 | 0 | 1 | 2 |
| `errno.h` | - | 0 | 0 | 0 | 0 | 148 |
| `eventfd.h` | fs | 8 | 5 | 3 | 0 | 5 |
| `exec_read_trace.h` | process | 6 | 6 | 0 | 0 | 3 |
| `fault.h` | core/fault | 3 | 2 | 1 | 0 | 14 |
| `fb.h` | drivers/video | 5 | 3 | 1 | 1 | 5 |
| `fcntl.h` | - | 0 | 0 | 0 | 0 | 21 |
| `fd_dispatch.h` | - | 6 | 5 | 0 | 1 | 2 |
| `fd_get.h` | process | 3 | 3 | 0 | 0 | 3 |
| `fd_install.h` | process | 1 | 0 | 0 | 1 | 3 |
| `fd_resource.h` | fs | 4 | 3 | 1 | 0 | 7 |
| `fd_types.h` | - | 0 | 0 | 0 | 0 | 10 |
| `files_struct.h` | process/syscalls | 11 | 4 | 7 | 0 | 13 |
| `flock.h` | fs | 2 | 1 | 1 | 0 | 3 |
| `fork.h` | arch (both) | 3 | 3 | 0 | 0 | 3 |
| `fpu.h` | arch (both) | 1 | 1 | 0 | 0 | 2 |
| `fpu_backend.h` | arch (both) | 7 | 0 | 7 | 0 | 14 |
| `futex.h` | process | 4 | 2 | 2 | 0 | 5 |
| `heart_src_probe.h` | - | 0 | 0 | 0 | 0 | 0 |
| `heartfs.h` | fs | 7 | 5 | 2 | 0 | 4 |
| `idle.h` | sched | 1 | 1 | 0 | 0 | 3 |
| `init_drv.h` | drivers/core | 1 | 1 | 0 | 0 | 2 |
| `init_handoff.h` | boot | 1 | 0 | 1 | 0 | 2 |
| `input.h` | drivers/core/drivers/input | 14 | 13 | 0 | 1 | 6 |
| `input_backend.h` | drivers/input/ktm | 18 | 17 | 1 | 0 | 15 |
| `interop.h` | - | 5 | 1 | 0 | 4 | 0 |
| `io_async.h` | syscalls | 1 | 1 | 0 | 0 | 3 |
| `ipc.h` | ipc | 26 | 10 | 14 | 2 | 8 |
| `irq.h` | arch (both) | 9 | 7 | 2 | 0 | 16 |
| `kernel.h` | boot | 1 | 1 | 0 | 0 | 1 |
| `kexec.h` | process | 3 | 3 | 0 | 0 | 3 |
| `keyboard.h` | arch/x86/drivers/input | 9 | 0 | 9 | 0 | 2 |
| `klog.h` | - | 0 | 0 | 0 | 0 | 16 |
| `klog_event.h` | ktm | 7 | 4 | 1 | 2 | 2 |
| `kmem.h` | - | 2 | 2 | 0 | 0 | 81 |
| `ktm/assert.h` | ktm | 10 | 0 | 7 | 3 | 1 |
| `ktm/checkpoint.h` | ktm | 2 | 1 | 0 | 1 | 6 |
| `ktm/deferred.h` | ktm | 2 | 1 | 1 | 0 | 4 |
| `ktm/event.h` | ktm | 7 | 2 | 5 | 0 | 7 |
| `ktm/fault.h` | ktm/tests | 4 | 1 | 3 | 0 | 7 |
| `ktm/klog.h` | - | 0 | 0 | 0 | 0 | 75 |
| `ktm/ktm.h` | ktm | 2 | 1 | 1 | 0 | 2 |
| `ktm/probe.h` | ktm | 5 | 0 | 4 | 1 | 2 |
| `ktm/scenario.h` | ktm/tests | 7 | 3 | 2 | 2 | 2 |
| `ktm/snapshot.h` | ktm | 2 | 0 | 1 | 1 | 2 |
| `ktm/stack_watch.h` | ktm | 6 | 6 | 0 | 0 | 4 |
| `ktm/transport.h` | ktm | 3 | 0 | 3 | 0 | 2 |
| `ktm/uapi.h` | - | 1 | 0 | 0 | 1 | 3 |
| `ktm/user_canary.h` | ktm | 3 | 3 | 0 | 0 | 4 |
| `ktm/userdev.h` | - | 1 | 1 | 0 | 0 | 4 |
| `logging.h` | - | 21 | 17 | 0 | 4 | 36 |
| `memfd.h` | mm | 9 | 8 | 1 | 0 | 8 |
| `mm.h` | arch (both) | 5 | 4 | 1 | 0 | 15 |
| `mm_alloc.h` | mm | 1 | 0 | 1 | 0 | 7 |
| `mm_diag.h` | mm | 1 | 0 | 1 | 0 | 2 |
| `mm_port.h` | ktm/mm | 4 | 4 | 0 | 0 | 10 |
| `mm_range.h` | - | 2 | 2 | 0 | 0 | 5 |
| `mm_struct.h` | process | 12 | 4 | 7 | 1 | 6 |
| `mman.h` | - | 0 | 0 | 0 | 0 | 6 |
| `mount_prefix.h` | fs | 1 | 0 | 1 | 0 | 5 |
| `multiboot.h` | - | 0 | 0 | 0 | 0 | 2 |
| `named_devnode.h` | fs | 4 | 3 | 1 | 0 | 3 |
| `named_fifo.h` | fs | 7 | 5 | 2 | 0 | 6 |
| `named_socket.h` | fs | 3 | 2 | 1 | 0 | 5 |
| `named_symlink.h` | fs | 5 | 5 | 0 | 0 | 4 |
| `net.h` | net | 39 | 26 | 10 | 3 | 27 |
| `net_stack.h` | - | 0 | 0 | 0 | 0 | 0 |
| `no_mmu.h` | mm | 4 | 0 | 4 | 0 | 2 |
| `oom.h` | mm/process | 11 | 3 | 8 | 0 | 9 |
| `oops.h` | - | 5 | 5 | 0 | 0 | 37 |
| `oops_port.h` | - | 0 | 0 | 0 | 0 | 8 |
| `open_flags.h` | fs | 4 | 4 | 0 | 0 | 9 |
| `overflow.h` | - | 3 | 3 | 0 | 0 | 8 |
| `page_fault.h` | arch (both) | 5 | 1 | 1 | 3 | 9 |
| `page_table_pool.h` | mm | 2 | 2 | 0 | 0 | 5 |
| `paging.h` | mm/tests | 41 | 29 | 10 | 2 | 44 |
| `partition.h` | drivers/storage | 6 | 3 | 1 | 2 | 6 |
| `path.h` | fs | 7 | 5 | 2 | 0 | 13 |
| `path_routed.h` | fs | 8 | 8 | 0 | 0 | 7 |
| `path_user.h` | fs | 4 | 4 | 0 | 0 | 6 |
| `percpu.h` | core/irq | 7 | 6 | 1 | 0 | 9 |
| `permissions.h` | fs/process | 6 | 4 | 0 | 2 | 20 |
| `pipe.h` | fs | 16 | 13 | 1 | 2 | 13 |
| `pipe_fd.h` | - | 5 | 5 | 0 | 0 | 6 |
| `platform_ops.h` | arch (both) | 2 | 1 | 1 | 0 | 5 |
| `platform_resource.h` | - | 0 | 0 | 0 | 0 | 4 |
| `pmm.h` | mm | 17 | 8 | 8 | 1 | 28 |
| `poll.h` | syscalls | 2 | 2 | 0 | 0 | 13 |
| `posix_shm.h` | ipc | 4 | 3 | 1 | 0 | 3 |
| `power_manag.h` | arch (both) | 4 | 1 | 0 | 3 | 3 |
| `problems.h` | core/fault | 5 | 1 | 2 | 2 | 4 |
| `process.h` | - | 0 | 0 | 0 | 0 | 86 |
| `process_domains.h` | process | 5 | 0 | 5 | 0 | 2 |
| `process_exit.h` | process | 3 | 1 | 2 | 0 | 4 |
| `process_introspect.h` | process | 2 | 2 | 0 | 0 | 4 |
| `process_limits.h` | process | 3 | 3 | 0 | 0 | 4 |
| `procfs.h` | fs | 51 | 4 | 41 | 6 | 4 |
| `product_ipc.h` | ipc | 1 | 0 | 0 | 1 | 1 |
| `product_vfs.h` | syscalls/syscalls/early | 2 | 0 | 0 | 2 | 2 |
| `ps2_mouse.h` | drivers/input | 9 | 1 | 7 | 1 | 2 |
| `ps2_mouse_pkt.h` | drivers/input | 2 | 0 | 2 | 0 | 3 |
| `ps2_set1.h` | drivers/input | 8 | 6 | 1 | 1 | 4 |
| `pseudo_fs.h` | fs | 22 | 6 | 11 | 5 | 14 |
| `pseudo_snapshot.h` | - | 1 | 0 | 0 | 1 | 0 |
| `pty_devfs.h` | fs | 14 | 13 | 1 | 0 | 2 |
| `ramtab.h` | fs | 15 | 11 | 3 | 1 | 5 |
| `reclaim.h` | mm | 2 | 0 | 2 | 0 | 9 |
| `resource_registry.h` | drivers/core/tests | 9 | 6 | 3 | 0 | 9 |
| `rtc.h` | drivers/rtc/arch/arm64 | 8 | 3 | 5 | 0 | 6 |
| `sched.h` | sched | 9 | 9 | 0 | 0 | 23 |
| `select.h` | - | 4 | 3 | 0 | 1 | 2 |
| `serial_io.h` | arch/arm64/drivers/serial | 7 | 4 | 2 | 1 | 19 |
| `sigcontext_arm64.h` | - | 0 | 0 | 0 | 0 | 1 |
| `sigcontext_x86_64.h` | - | 0 | 0 | 0 | 0 | 2 |
| `signal_irq.h` | arch (both) | 12 | 9 | 1 | 2 | 6 |
| `signals.h` | signals | 20 | 17 | 2 | 1 | 29 |
| `sigtramp.h` | signals | 2 | 2 | 0 | 0 | 3 |
| `smp_convergence.h` | - | 0 | 0 | 0 | 0 | 0 |
| `sock_icmp.h` | net | 12 | 11 | 1 | 0 | 8 |
| `sock_inet_ioctl.h` | net | 1 | 1 | 0 | 0 | 2 |
| `sock_stream.h` | net | 40 | 34 | 3 | 3 | 10 |
| `sock_udp.h` | net | 9 | 9 | 0 | 0 | 5 |
| `socket.h` | - | 4 | 4 | 0 | 0 | 4 |
| `sound_blaster.h` | sound | 13 | 3 | 5 | 5 | 4 |
| `spinlock.h` | - | 2 | 2 | 0 | 0 | 22 |
| `stat.h` | - | 7 | 7 | 0 | 0 | 43 |
| `stat_user.h` | fs | 2 | 1 | 1 | 0 | 3 |
| `statfs.h` | fs | 1 | 1 | 0 | 0 | 7 |
| `swap.h` | mm/tests | 19 | 7 | 12 | 0 | 13 |
| `syscall.h` | - | 58 | 1 | 0 | 57 | 3 |
| `syscall_arm64.h` | - | 5 | 5 | 0 | 0 | 1 |
| `syscall_frame.h` | arch (both) | 3 | 3 | 0 | 0 | 8 |
| `syscall_id.h` | arch (both) | 1 | 1 | 0 | 0 | 10 |
| `syscall_identity.h` | syscalls | 2 | 0 | 0 | 2 | 2 |
| `syscall_table.h` | syscalls | 5 | 3 | 2 | 0 | 14 |
| `syscall_x86_64.h` | - | 10 | 5 | 0 | 5 | 1 |
| `sysfs.h` | fs/boot | 41 | 12 | 25 | 4 | 10 |
| `sysv_shm.h` | ipc | 4 | 4 | 0 | 0 | 3 |
| `task.h` | - | 6 | 1 | 0 | 5 | 13 |
| `task_ops.h` | arch (both) | 9 | 7 | 0 | 2 | 9 |
| `time.h` | - | 0 | 0 | 0 | 0 | 20 |
| `timerfd.h` | fs | 8 | 5 | 3 | 0 | 5 |
| `tlb.h` | arch (both) | 4 | 4 | 0 | 0 | 7 |
| `tlb_sync.h` | mm | 2 | 0 | 2 | 0 | 8 |
| `tls.h` | arch (both) | 6 | 2 | 1 | 3 | 7 |
| `types.h` | - | 3 | 1 | 0 | 2 | 61 |
| `typewriter.h` | drivers/video | 9 | 8 | 0 | 1 | 3 |
| `uio.h` | - | 0 | 0 | 0 | 0 | 5 |
| `usb_host.h` | drivers/usb | 3 | 1 | 2 | 0 | 3 |
| `utimens.h` | fs | 1 | 1 | 0 | 0 | 5 |
| `utsname.h` | - | 2 | 2 | 0 | 0 | 5 |
| `utsname_info.h` | boot | 2 | 2 | 0 | 0 | 3 |
| `vbe.h` | drivers/video | 16 | 1 | 11 | 4 | 3 |
| `vdso.h` | arch (both) | 4 | 4 | 0 | 0 | 2 |
| `version.h` | - | 0 | 0 | 0 | 0 | 7 |
| `vfs.h` | - | 0 | 0 | 0 | 0 | 19 |
| `vfs_backend.h` | - | 0 | 0 | 0 | 0 | 3 |
| `vfs_dispatch.h` | fs | 5 | 4 | 0 | 1 | 3 |
| `vfs_file.h` | process | 6 | 4 | 2 | 0 | 7 |
| `vga.h` | drivers/char | 10 | 5 | 4 | 1 | 18 |
| `video_backend.h` | drivers/video/ktm | 10 | 6 | 4 | 0 | 11 |
| `video_console.h` | drivers/video | 9 | 9 | 0 | 0 | 4 |
| `virtio_9p.h` | drivers/virtio | 16 | 15 | 0 | 1 | 5 |
| `virtio_mmio.h` | drivers/virtio | 6 | 6 | 0 | 0 | 3 |
| `wait.h` | - | 5 | 2 | 2 | 1 | 2 |

## Appendix B. Symbols with no reference outside their header

Grep-level; includes private headers. Re-verify each before deleting (rust/ is not scanned).

- `arch/arm64/sources/exc_early.h`: `arm64_psci_system_off`, `arm64_psci_system_reset`
- `arch/common/common_paging.h`: `paging_set_cpu`, `paging_set_cpu_x64`
- `arch/x86-64/include/asm/debug_trap.h`: `ir0_rflags_clear_tf`
- `arch/x86-64/include/asm/process_ctx_invariant.h`: `process_cs_rip_kernel_ret_bad`, `process_rip_in_user_range`, `process_rip_in_user_stack`
- `arch/x86-64/include/interrupt/keyboard.h`: `keyboard_modifiers_mask`, `keyboard_ctrl_active`, `set_idle_mode`, `is_in_idle_mode`, `wakeup_from_idle`
- `arch/x86-64/include/interrupt/pic.h`: `pic_mask_irq`
- `arch/x86-64/sources/tss.h`: `tss_init_x64`, `tss_load_x64`, `tss_get_address`, `tss_get_size`
- `drivers/bluetooth/bluetooth_init.h`: `bluetooth_is_initialized`, `bluetooth_get_status`
- `drivers/bluetooth/bt_device.h`: `bt_device_get_manager`
- `drivers/bluetooth/hci_core.h`: `hci_remote_name_request`
- `drivers/bluetooth/hci_uart.h`: `hci_uart_send_acl`, `hci_uart_receive_acl`, `hci_uart_get_instance`
- `drivers/char/pc_speaker.h`: `pc_speaker_beep`, `pc_speaker_set_enabled`
- `drivers/input/ps2.h`: `ps2_send_command`, `ps2_read_data`, `ps2_wait_for_output`, `ps2_wait_for_input`, `ps2_keyboard_init`, `ps2_keyboard_send_command`, `ps2_keyboard_read_scancode`, `ps2_scancode_to_ascii`, `ps2_is_key_pressed`, `ps2_is_shift_pressed`, `ps2_is_ctrl_pressed`, `ps2_is_alt_pressed`, `ps2_keyboard_handler`, `ps2_get_char`, `ps2_has_char`, `ps2_flush_buffer`, `keyboard_shift_pressed`, `keyboard_ctrl_pressed`, `keyboard_alt_pressed`
- `drivers/input/ps2_mouse.h`: `ps2_mouse_set_scaling_1_1`, `ps2_mouse_read_data`
- `drivers/net/bcmgenet.h`: `bcmgenet_mdio_write_cmd`
- `drivers/net/rtl8139.h`: `rtl8139_get_mac`
- `drivers/storage/ahci.h`: `ahci_disk_present`, `ahci_sector_count`
- `drivers/storage/ata.h`: `ata_is_available`, `ata_get_device_info`, `ata_get_model`, `ata_get_serial`
- `drivers/storage/fs_types.h`: `get_fs_type`
- `drivers/storage/nvme.h`: `nvme_disk_present`, `nvme_sector_count`
- `drivers/timer/acpi/acpi.h`: `find_hpet_table`
- `drivers/timer/hpet/hpet.h`: `hpet_set_address`
- `drivers/timer/lapic/lapic.h`: `lapic_init_timer`, `lapic_available`, `lapic_send_eoi`
- `drivers/video/console_renderer.h`: `console_renderer_get_cursor_y`, `console_renderer_import_cursor_pos`
- `fs/minix_fs.h`: `minix_fs_is_available`, `minix_fs_cat`, `minix_fs_read_file`, `minix_fs_rmdir_force`, `minix_fs_cleanup`, `minix_mark_inode_used`, `minix_mark_inode_free`, `minix_is_chr`, `minix_is_blk`, `minix_get_uid_perms`, `minix_get_gid_perms`, `minix_get_oth_perms`
- `fs/tmpfs.h`: `tmpfs_is_available`
- `fs/vfs.h`: `vfs_init_with_minix`, `vfs_mount_is_nosuid`
- `includes/ir0/abi/mmap_contract.h`: `ir0_mmap_align_down`, `ir0_mmap_ranges_overlap`
- `includes/ir0/acpi_pm.h`: `ir0_acpi_pm_s3_ok`
- `includes/ir0/arch_info.h`: `late_init`, `get_cmdline`, `get_arch_bits`, `supports_feature`, `get_arch_cflags`, `get_arch_ldflags`, `dump_cpu_registers`
- `includes/ir0/arch_io.h`: `mmio_read8`, `mmio_write8`, `mmio_read16`, `mmio_write16`, `mmio_read32`, `mmio_write32`
- `includes/ir0/arch_task.h`: `task_context_init`, `IR0_TASK_ACCESSOR`, `task_get_ss`, `task_set_user_return`, `task_prepare_fork_child`
- `includes/ir0/bluetooth.h`: `ir0_bt_proc_scan_write`
- `includes/ir0/boot_memory.h`: `boot_memory_get_reserved_ranges`
- `includes/ir0/bpath.h`: `early_rootfs_bpath_used`
- `includes/ir0/bpivot.h`: `ir0_bpivot_tree_count`
- `includes/ir0/chmod.h`: `parse_mode`
- `includes/ir0/compat.h`: `operator_new_array`, `operator_delete_array`
- `includes/ir0/copy_user.h`: `clear_user`, `put_user`, `get_user`
- `includes/ir0/cpu_info.h`: `get_page_size`, `get_cpu_mode`, `timer_set_frequency`, `timer_get_frequency`
- `includes/ir0/cred_transition.h`: `ir0_cred_id_known`
- `includes/ir0/credentials.h`: `ir0_current_umask`
- `includes/ir0/cwd.h`: `ir0_product_cwd_provider`
- `includes/ir0/debug_runtime.h`: `IR0_DBG_PMM`, `IR0_DBG_WAIT`, `IR0_DBG_PROC`
- `includes/ir0/devfs.h`: `devfs_fd_can_write`
- `includes/ir0/early_cpio.h`: `ir0_early_hex_nibble`
- `includes/ir0/early_io.h`: `early_io_smoke_ok`, `early_io_provider`
- `includes/ir0/early_process.h`: `early_process_smoke_ok`, `early_process_clone_take_enter`, `early_process_clone_save_parent`, `early_process_clone_resume_parent`, `early_process_take_yield`, `early_process_block_wait_observed`, `early_process_vfork_exec_resume`, `early_process_bind_common_exit`, `early_process_bind_schedule_exit`, `early_process_arm_schedule_exit`, `early_process_common_exit_published`, `early_process_bind_identity`, `early_process_identity_used`, `early_process_bind_files`, `early_process_provider`, `early_process_identity_provider`
- `includes/ir0/early_rootfs.h`: `early_rootfs_init_blob`, `early_rootfs_init_from_block`, `early_rootfs_bpivot_done`, `early_rootfs_memdev_ready`, `early_rootfs_dents_ready`, `early_rootfs_vda_ready`, `early_rootfs_smoke_init`, `early_rootfs_bind_cwd`, `early_rootfs_cwd_used`, `early_rootfs_bind_fdtable`, `early_rootfs_fdtable_used`, `early_rootfs_vfs_file_used`, `early_rootfs_bind_vfs_ops`, `early_rootfs_vfs_ops_used`, `early_rootfs_exec_pending`, `early_rootfs_exec_argv`, `early_rootfs_exec_clear`, `early_rootfs_readlinkat`, `ir0_early_dirent64_reclen`, `ir0_early_put_le16`
- `includes/ir0/early_syscall.h`: `early_syscall_dispatch_invoke`
- `includes/ir0/early_time.h`: `early_time_provider`
- `includes/ir0/early_vfs.h`: `early_vfs_provider`, `early_vfs_cwd_provider`
- `includes/ir0/elf_loader.h`: `load_elf_program`, `debug_elf_header`, `debug_program_header`
- `includes/ir0/epoll.h`: `epoll_release_fd`
- `includes/ir0/fb.h`: `ir0_fb_boot_direct_draw`
- `includes/ir0/fd_dispatch.h`: `fd_entry_pipe`
- `includes/ir0/fd_install.h`: `ir0_fd_install_product`
- `includes/ir0/input.h`: `ir0_input_is_available`
- `includes/ir0/interop.h`: `ir0_rust_panic_handler`, `delete`, `IR0_ALIGNED`, `IR0_SOME`
- `includes/ir0/ipc.h`: `semaphore_up`, `ipc_channel_find`
- `includes/ir0/klog_event.h`: `klog_get_boot_phase`, `klog_record_count`
- `includes/ir0/ktm/assert.h`: `ktm_assert_no_process_leak`, `ktm_assert_no_frame_leak`, `KTM_V1_ASSERT_EQ`
- `includes/ir0/ktm/checkpoint.h`: `ktm_checkpoint_emit`
- `includes/ir0/ktm/probe.h`: `ktm_write_cstr`
- `includes/ir0/ktm/scenario.h`: `ktm_suite_pass_count`, `ktm_suite_fail_count`
- `includes/ir0/ktm/snapshot.h`: `ktm_snapshot_diff`
- `includes/ir0/ktm/uapi.h`: `KTM_DUMP_ARG`
- `includes/ir0/logging.h`: `log_fatal`, `log_fatal_fmt`, `LOG_FATAL`, `LOG_FATAL_FMT`
- `includes/ir0/mm_struct.h`: `process_mm_adopt_active`
- `includes/ir0/net.h`: `net_unregister_protocol`, `ir0_host_to_be16`, `ir0_host_to_be32`
- `includes/ir0/page_fault.h`: `get_fault_address`, `get_fault_type`, `get_fault_error`
- `includes/ir0/paging.h`: `setup_and_enable_paging`, `unmap_page`
- `includes/ir0/partition.h`: `is_gpt_disk`, `get_partition_type`
- `includes/ir0/permissions.h`: `get_current_uid`, `get_current_gid`
- `includes/ir0/pipe.h`: `pipe_acquire`, `pipe_ipc_lifecycle_audit`
- `includes/ir0/pmm.h`: `pmm_init`
- `includes/ir0/power_manag.h`: `system_halt`, `system_reboot`, `system_poweroff`
- `includes/ir0/problems.h`: `bug_report`, `oops_report`
- `includes/ir0/procfs.h`: `proc_open`, `proc_read`, `proc_write`, `proc_get_offset`, `proc_set_offset`, `proc_getdents`
- `includes/ir0/product_ipc.h`: `ir0_product_ipc_provider`
- `includes/ir0/product_vfs.h`: `ir0_product_vfs_provider`, `early_vfs_path_provider`
- `includes/ir0/ps2_mouse.h`: `ps2_mouse_handle_interrupt`
- `includes/ir0/ps2_set1.h`: `ps2_set1_alt`
- `includes/ir0/pseudo_fs.h`: `pseudo_fs_find_by_fd`, `pseudo_fs_proc_init`, `pseudo_fs_sys_init`, `pseudo_fs_write_fd`, `pseudo_fs_stat_fd`
- `includes/ir0/pseudo_snapshot.h`: `pseudo_snapshot_slice`
- `includes/ir0/ramtab.h`: `ramtab_get`
- `includes/ir0/select.h`: `IR0_FD_CLR`
- `includes/ir0/serial_io.h`: `serial_read_char`
- `includes/ir0/signal_irq.h`: `sigcontext_arg`, `sigcontext_set_arg`
- `includes/ir0/signals.h`: `signal_ignore`
- `includes/ir0/sock_stream.h`: `sock_stream_connect_inet`, `sock_stream_rights_push`, `sock_stream_is_recv_shutdown`
- `includes/ir0/sound_blaster.h`: `sb16_create_sample`, `sb16_destroy_sample`, `sb16_play_sample`, `sb16_stop_playback`, `sb16_is_playing`
- `includes/ir0/syscall.h`: `ir0_exit`, `ir0_fork`, `ir0_exec`, `ir0_execve`, `ir0_waitpid`, `ir0_open`, `ir0_creat`, `ir0_close`, `ir0_read`, `ir0_write`, `ir0_lseek`, `ir0_ioctl`, `ir0_stat`, `ir0_fstat`, `ir0_brk`, `ir0_mmap`, `ir0_munmap`, `ir0_getpid`, `ir0_getuid`, `ir0_geteuid`, `ir0_getgid`, `ir0_getegid`, `ir0_setuid`, `ir0_setgid`, `ir0_umask`, `ir0_getcwd`, `ir0_chdir`, `ir0_chroot`, `ir0_mount`, `ir0_umount`, `ir0_unlink`, `ir0_link`, `ir0_rename`, `ir0_access`, `ir0_dup`, `ir0_dup2`, `ir0_chmod`, `ir0_ps`, `ir0_ls`, `ir0_touch`, `ir0_rm`, `ir0_mkdir`, `ir0_rmdir`, `ir0_poll`, `ir0_nanosleep`, `ir0_clock_gettime`, `ir0_gettimeofday`, `ir0_netinfo`, `ir0_lsdrv`, `ir0_audio_test`, `ir0_mouse_test`, `ir0_dmesg`, `ir0_ping`, `ir0_ifconfig`, `ir0_df`, `ir0_malloc_test`, `ir0_getppid`
- `includes/ir0/syscall_identity.h`: `ir0_syscall_identity_product`, `ir0_product_identity_provider`
- `includes/ir0/syscall_x86_64.h`: `syscall_linux0`, `syscall_linux1`, `syscall_linux2`, `syscall_linux3`, `syscall_linux6`
- `includes/ir0/sysfs.h`: `sysfs_open`, `sysfs_read`, `sysfs_write`, `sys_kernel_process_live_count`
- `includes/ir0/task.h`: `TASK_INIT`, `task_is_ready`, `task_is_running`, `task_is_blocked`, `task_is_terminated`
- `includes/ir0/task_ops.h`: `task_retval_slot_addr`, `task_store_sigcontext`
- `includes/ir0/tls.h`: `tls_get_base`, `set_user_tls`, `tls_invalidate`
- `includes/ir0/types.h`: `IR0_MAJOR`, `IR0_MINOR`
- `includes/ir0/typewriter.h`: `typewriter_print_char`
- `includes/ir0/vbe.h`: `vbe_clear`, `vbe_putpixel`, `vbe_print_at`, `VBE_RGB`
- `includes/ir0/vfs_dispatch.h`: `vfs_dispatch_stat`
- `includes/ir0/vga.h`: `console_puts`
- `includes/ir0/virtio_9p.h`: `virtio_9p_read_file`
- `includes/ir0/wait.h`: `ir0_make_wait_status`
- `kernel/elf_loader.h`: `elf_load_and_execute`
- `kernel/process.h`: `fork_restore_emit_pre_iretq`, `fork_flow_note_kernel_entry`, `process_guest_install`, `process_guest_rearm`, `process_exit_guest`, `process_sched_state_trace`, `process_has_zombie_child`, `process_count_resident_user_pages`, `process_list_count`, `process_list_count_user`, `process_set_pgd`, `process_syscall_flags`, `process_syscall_set_ip`, `process_syscall_set_sp`, `process_syscall_set_flags`, `process_syscall_set_arg`
- `kernel/process/process_internal.h`: `process_duplicate_fd_table`
- `kernel/syscalls/io_syscalls.h`: `syscall_wake_blocked_on_child`, `syscall_poll_finish_blocked_resume`
- `kernel/syscalls/process_syscalls.h`: `process_cred_in_group`, `process_reset_signals_on_exec`
- `ktm/include/klog.h`: `kprintf_level`, `klog_get_profile`
- `ktm/include/ktm.h`: `ktm_panic_site_emit`, `ktm_classify_kernel_panic`, `ktm_classify_user_fault`, `ktm_invariant_process`, `KTM_MARK_FAIL`, `KTM_ASSERT`, `KTM_ASSERT_EQ`, `KTM_ASSERT_GE`, `KTM_ASSERT_GT`, `KTM_TRACE_SCHED_SWITCH`, `KTM_EVENT_PID`, `KTM_INVARIANT`, `KTM_INVARIANT_PROC`, `KTM_PROBE`, `KTM_PROBE_CTX`, `KTM_TRACE`, `KTM_BEGIN`, `KTM_END`
- `ktm/include/ktm_internal.h`: `ktm_now_ticks`
- `ktm/include/ktm_mock_audio.h`: `ktm_audio_mock_set_volume`
- `ktm/include/ktm_mock_console.h`: `ktm_console_mock_set_printk_to_screen`, `ktm_console_mock_set_serial_mirror`
- `ktm/include/ktm_mock_input.h`: `ktm_input_mock_set_layout`, `ktm_input_mock_set_console_mode`
- `net/arp.h`: `arp_lookup`
- `net/ip.h`: `ip_format_addr`, `IP_HEADER_LEN`
- `sound/adlib.h`: `adlib_is_available`
- `sound/sound_blaster.h`: `sb16_get_master_volume`, `sb16_set_pcm_volume`, `sb16_get_pcm_volume`, `sb16_is_speaker_on`, `sb16_pause_playback`, `sb16_resume_playback`, `sb16_setup_dma_16bit`

## Appendix C. Public symbols used only by their owner (or only by tests)

Candidates for `static` or a subsystem-private header. Symbols defined by both ISAs and used only by arch code are arch-private hooks.

- `abi/brk_contract.h`: `ir0_page_align_up`, `ir0_elf_initial_brk_from_phdr`
- `abi/mmap_contract.h`: `ir0_mmap_is_kernel_error`, `ir0_mmap_kernel_errno`, `ir0_mmap_libc_ret`, `ir0_mmap_fixed_requires_exact_va`, `ir0_mmap_in_musl_arena`, `ir0_mmap_musl_next_slot`, `ir0_mmap_page_aligned`, `ir0_mmap_occupy`, `ir0_mmap_heap_low_limit`, `ir0_mmap_pick_topdown`
- `acpi_pm.h`: `ir0_acpi_pm_init`, `ir0_acpi_pm_try_poweroff`
- `arch_io.h`: `read_fault_address`, `cpu_wait`
- `arch_mm.h`: `mm_map_platform_supervisor`
- `blockdev.h`: `ir0_block_flush`, `ir0_block_dev_at`, `ir0_block_reset_for_test`, `ir0_block_unregister_for_test`
- `boot_flow.h`: `ir0_kernel_boot`
- `boot_log_hostshare.h`: `ir0_boot_log_hostshare_try`
- `boot_memory.h`: `boot_memory_range_is_reserved`, `boot_memory_get_usable_ranges`, `boot_memory_normalize_ranges`, `boot_memory_exclude_reserved`, `boot_memory_get_allocatable_ranges`
- `boot_params.h`: `set_boot_params`
- `bpath.h`: `ir0_bpath_is_reserved_early`, `ir0_bpath_is_tmp`, `bpath_live`
- `bpivot.h`: `ir0_bpivot_mount_trees`, `ir0_bpivot_prepare_staging`, `ir0_bpivot_commit`, `ir0_bpivot_committed`, `ir0_bpivot_root_switched`, `ir0_bpivot_trees`
- `clock_wait.h`: `ir0_clock_wait_arm`, `ir0_clock_wait_fire_due`
- `cmdline.h`: `ir0_cmdline_apply_log_profile`
- `console.h`: `tty_input_char`, `tty_read_kernel`, `tty_write_kernel`, `tty_flush_input`, `ir0_console_has_blocked_reader`, `ir0_console_input_enqueue`
- `console_backend.h`: `console_backend_fb_scale`, `console_backend_show_cursor`, `console_backend_cursor_x`, `console_backend_set_tty_serial_mirror`, `console_backend_typewriter_init`
- `context.h`: `switch_to`
- `cpu.h`: `cpu_possible_mask`, `cpu_present_mask`, `cpu_online_mask`, `cpu_active_mask`, `cpu_online_count`, `cpu_is_present`, `cpu_is_online`, `cpu_is_active`, `wrmsr`, `debug_reg_write`, `debug_reg_read`
- `devfs.h`: `devfs_find_node_by_id`, `devfs_chown_path`, `devfs_stat_path`, `devfs_readdir_root`, `devfs_readdir_subdir`, `dev_null_read`, `dev_null_write`, `dev_zero_read`, `dev_zero_write`, `dev_console_read`, `dev_console_write`, `dev_kmsg_read`, `dev_kmsg_write`, `dev_audio_read`, `dev_audio_write`, `dev_audio_ioctl`, `dev_mouse_read`, `dev_mouse_ioctl`, `dev_net_read`, `dev_net_write`, `dev_net_ioctl`, `dev_disk_read`, `dev_disk_write`, `dev_disk_ioctl`, `dev_null`, `dev_zero`, `dev_console`, `dev_tty`, `dev_audio`, `dev_mouse`, `dev_net`, `dev_disk`, `dev_kmsg`
- `driver.h`: `ir0_driver_boot_counts`, `ir0_driver_boot_emit_probe_results`, `ir0_driver_registry_is_initialized`
- `driver_bootstrap.h`: `driver_bootstrap_register`, `driver_bootstrap_run_all`, `driver_bootstrap_reset`
- `early_anonfd.h`: `early_ipc_ring_consume`, `early_ipc_ring_peek`, `early_ipc_ring_deposit`
- `early_cpio.h`: `ir0_early_cpio_hex8`, `ir0_early_cpio_align4`, `ir0_early_cpio_is_trailer`
- `early_elf_stack.h`: `ir0_early_elf_setup_stack`
- `early_mm.h`: `early_mm_activate`, `early_mm_provider`
- `early_process.h`: `early_process_is_root`, `early_process_request_yield`, `early_process_current_pid`, `early_process_current_ucred`, `early_process_note_exec`, `ir0_early_clone_flags_ok`, `ir0_early_process_getpgid`, `ir0_early_process_getsid`, `ir0_early_process_setsid`, `ir0_early_process_setpgid`, `ir0_early_process_kill`, `ir0_early_process_tkill`, `ir0_early_process_tgkill`, `ir0_early_sched_policy_ok`, `ir0_early_sched_policy_rt`
- `early_rootfs.h`: `early_rootfs_bind`, `early_rootfs_bind_cpio`, `early_rootfs_block_write`, `early_rootfs_fd_is_block`, `early_rootfs_console_ready`, `early_rootfs_fd_is_console`, `early_rootfs_fd_discards_write`, `early_rootfs_fd_is_pipe_write`, `early_rootfs_pipe_write`, `early_rootfs_eventfd2`, `early_rootfs_eventfd_write`, `early_rootfs_fd_is_eventfd`, `early_rootfs_note_clock`, `early_rootfs_memfd_create`, `early_rootfs_memfd_write`, `early_rootfs_fd_is_memfd`, `early_rootfs_timerfd_create`, `early_rootfs_timerfd_settime`, `early_rootfs_timerfd_gettime`, `early_rootfs_epoll_create1`, `early_rootfs_epoll_ctl`, `early_rootfs_epoll_pwait`, `early_rootfs_socket`, `early_rootfs_sock_bind`, `early_rootfs_sock_listen`, `early_rootfs_sock_connect`, `early_rootfs_sock_accept4`, `early_rootfs_getsockname`, `early_rootfs_getpeername`, `early_rootfs_sendto`, `early_rootfs_recvfrom`, `early_rootfs_shutdown`, `early_rootfs_setsockopt`, `early_rootfs_getsockopt`, `early_rootfs_symlinkat`, `early_rootfs_linkat`, `early_rootfs_sock_write`, `early_rootfs_fd_is_sock`, `early_rootfs_fd_is_file`, `early_rootfs_file_write`, `early_rootfs_fd_is_fifo`, `early_rootfs_fifo_write`, `early_rootfs_blkgetsize64`, `early_rootfs_dup`, `early_rootfs_dup3`, `early_rootfs_getcwd`, `early_rootfs_getdents64`, `early_rootfs_lseek`, `early_rootfs_fcntl`, `early_rootfs_fchdir`, `early_rootfs_fsync`, `early_rootfs_execve`, `early_rootfs_abstract_observed`, `early_rootfs_read`, `early_rootfs_close`, `early_rootfs_faccessat`, `early_rootfs_pread64`, `early_rootfs_pwrite64`, `early_rootfs_sendfile`, `early_rootfs_fallocate`, `early_rootfs_statx`, `early_rootfs_poll_revents`, `early_rootfs_statfs`, `early_rootfs_fstatfs`, `early_rootfs_fchmod`, `early_rootfs_fchmodat`, `early_rootfs_fchmodat2`, `early_rootfs_xattr`, `early_rootfs_xattrat`, `early_rootfs_name_to_handle_at`, `early_rootfs_open_by_handle_at`, `early_rootfs_cachestat`, `early_rootfs_file_getattr`, `early_rootfs_file_setattr`, `early_rootfs_landlock_create`, `early_rootfs_landlock_add`, `early_rootfs_landlock_restrict`, `early_rootfs_setns`, `early_rootfs_statmount`, `early_rootfs_listmount`, `early_rootfs_fchown`, `early_rootfs_fchownat`, `early_rootfs_chroot`, `early_rootfs_truncate`, `early_rootfs_ftruncate`, `early_rootfs_flock`, `early_rootfs_utimensat`, `early_rootfs_mkdirat`, `early_rootfs_mknodat`, `early_rootfs_unlinkat`, `early_rootfs_renameat`, `early_rootfs_renameat2`, `early_rootfs_send_rights`, `early_rootfs_recv_rights`, `early_rootfs_splice`, `early_rootfs_tee`, `early_rootfs_close_range`, `early_rootfs_copy_file_range`, `early_rootfs_fadvise64`, `early_rootfs_vmsplice`, `early_rootfs_sync_file_range`, `early_rootfs_readahead`, `early_rootfs_syncfs`, `early_rootfs_pidfd_open`, `early_rootfs_pidfd_pid`, `early_rootfs_kcmp_file`, `early_rootfs_pidfd_getfd`, `early_rootfs_faccessat2`, `early_rootfs_openat2`, `early_rootfs_want_creds`, `early_rootfs_take_creds`, `early_rootfs_send_creds`, `ir0_early_dirent64_pack`, `ir0_early_rootfs_dir_count`, `ir0_early_rootfs_dir_entry`, `ir0_early_rootfs_path_eq`, `ir0_early_rootfs_path_node`, `ir0_early_rootfs_fill_stat`, `ir0_early_put_le32`, `ir0_early_put_le64`, `ir0_early_statx_from_stat`, `ir0_early_timerfd_clock_ok`, `ir0_early_timerfd_create_flags_ok`, `ir0_early_memfd_flags_ok`, `ir0_early_epoll_create_flags_ok`, `ir0_early_socketpair_args_ok`, `ir0_early_socket_args_ok`, `ir0_early_msghdr_has_cmsg`, `ir0_early_cmsg_align`, `ir0_early_renameat2_flags_ok`, `ir0_early_splice_flags_ok`, `ir0_early_close_range_flags_ok`, `ir0_early_fadvise_ok`, `ir0_early_sync_file_range_flags_ok`, `ir0_early_sockaddr_un_is_abstract`, `ir0_early_sockaddr_un_abs_len`, `ir0_early_sockaddr_un_path`, `ir0_early_timespec_to_ticks`, `ir0_early_le16`, `ir0_early_le32`, `ir0_early_le64`, `ir0_early_elf64_image_bytes`
- `early_syscall.h`: `early_syscall_dispatch_init`
- `early_time.h`: `early_time_realtime_offset`, `early_time_set_realtime`
- `elf64_image.h`: `ir0_elf64_image_load`
- `eventfd.h`: `ir0_eventfd_is`, `ir0_eventfd_acquire`, `ir0_eventfd_release`
- `fault.h`: `ir0_fault_kind_name`
- `fb.h`: `ir0_fb_write_rect`
- `fd_resource.h`: `fd_resource_forget`
- `files_struct.h`: `files_create`, `files_get`, `files_put`, `process_files_bind`, `fd_slot_note_destroyed`, `process_files_share`, `process_files_clone`
- `flock.h`: `ir0_flock_release_file`
- `fpu_backend.h`: `fpu_init`, `fpu_switch`, `fpu_copy_current`, `fpu_save_sigcontext`, `fpu_signal_enter`, `fpu_sanitize_sigcontext`, `fpu_load_sigcontext`
- `futex.h`: `ir0_futex_drop_process`, `ir0_futex_before_sleep`
- `heartfs.h`: `heart_stat`, `heart_getdents`
- `init_handoff.h`: `ir0_init_handoff_run`
- `input_backend.h`: `input_kbd_get_layout_name`
- `ipc.h`: `wait_queue_init`, `wait_queue_wake_one`, `semaphore_init`, `ring_buffer_init`, `ring_buffer_destroy`, `ring_buffer_write`, `ring_buffer_read`, `ring_buffer_available_write`, `ring_buffer_available_read`, `ring_buffer_empty`, `ring_buffer_full`, `ipc_channel_create`, `ipc_channel_destroy`, `ipc_sleep_recheck_probe`
- `irq.h`: `irq_tables_init`, `irq_controller_init`
- `keyboard.h`: `keyboard_buffer_get`, `keyboard_buffer_has_data`, `keyboard_buffer_clear`, `keyboard_all_keys_up`, `keyboard_set_layout`, `keyboard_get_layout`, `keyboard_get_layout_name`, `keyboard_set_console_mode`, `keyboard_get_console_mode`
- `klog_event.h`: `klog_boot_phase_string`
- `ktm/assert.h`: `ktm_assert_result`, `ktm_assert_batch_begin`, `ktm_assert_batch_end`, `KTM_V1_ASSERT_TRUE`, `KTM_REQUIRE`, `KTM_ASSERT_NO_PROCESS_LEAK`, `KTM_ASSERT_NO_FRAME_LEAK`
- `ktm/deferred.h`: `ktm_deferred_flush`
- `ktm/event.h`: `ktm_event_emit`, `ktm_event_emit_pid`, `ktm_event_copy_out`, `ktm_event_pending`, `ktm_event_ring_reset_cursor`
- `ktm/fault.h`: `ktm_fault_seed`, `ktm_fault_configure`, `ktm_fault_should_fail`
- `ktm/ktm.h`: `ktm_invariants_run`
- `ktm/probe.h`: `ktm_write_u64`, `ktm_probe_register`, `ktm_probe_run`, `ktm_probes_register_builtins`
- `ktm/scenario.h`: `ktm_scenario_register`, `ktm_suite_reset`
- `ktm/snapshot.h`: `ktm_snapshot_take`
- `ktm/transport.h`: `ktm_transport_emit`, `ktm_transport_emit_u64`, `ktm_transport_suite_end`
- `memfd.h`: `ir0_memfd_is`
- `mm.h`: `mm_root_slots`
- `mm_alloc.h`: `mm_alloc_frame`
- `mm_diag.h`: `mm_diag_fault_hit`
- `mm_struct.h`: `mm_create`, `mm_users`, `mm_init_root`, `process_mm_bind`, `process_mm_share`, `exec_detach_shared_mm`, `process_mm_owns_tables`
- `mount_prefix.h`: `ir0_mount_prefix_boundary_ok`
- `named_devnode.h`: `named_devnode_stat`
- `named_fifo.h`: `named_fifo_is_runsv_supervise_path`, `named_fifo_create`
- `named_socket.h`: `named_socket_stat`
- `net.h`: `net_unregister_device`, `net_register_protocol`, `net_find_protocol_by_ethertype`, `net_find_protocol_by_ipproto`, `net_send`, `net_stack_get_stats`, `ip_route_add`, `ip_route_del`, `dns_set_default_server`, `make_ip4_addr`
- `no_mmu.h`: `no_mmu_configure`, `no_mmu_user_range_ok`, `no_mmu_physical_range_ok`, `no_mmu_virtual_remap_status`
- `oom.h`: `oom_candidate_eligible`, `oom_candidate_badness`, `oom_candidate_better`, `oom_select_candidate`, `oom_kill_process`, `oom_out_of_memory`, `oom_handle_alloc_failure`, `oom_kill_count`
- `page_fault.h`: `page_fault_decode`
- `paging.h`: `enable_paging`, `paging_copy_phys_page`, `is_paging_enabled`, `paging_install_pte`, `paging_ensure_4k_leaf`, `paging_test_poison_freed_tables`, `paging_ir0_mm_category_stats`, `paging_steady_state_audit`, `paging_oom_audit`, `paging_unmap_test_hook`
- `partition.h`: `read_partition_table`
- `path.h`: `ir0_path_apply_root`, `ir0_path_under_root`
- `percpu.h`: `cpu_local_by_id`
- `pipe.h`: `pipe_stats_get`
- `platform_ops.h`: `ir0_platform_ops_set`
- `pmm.h`: `pmm_init_ranges`, `pmm_frame_set_owner_class`, `pmm_frame_owner_class`, `pmm_get_start`, `pmm_get_end`, `pmm_frame_is_used`, `pmm_frame_owner`, `pmm_frame_phys`
- `posix_shm.h`: `posix_shm_ensure_dir`
- `problems.h`: `BUG`, `OOPS`
- `process_domains.h`: `process_cred_clone`, `process_signals_clone`, `process_rel_init_child`, `process_mm_cursor_clone`, `process_session_attrs_clone`
- `process_exit.h`: `process_exit_bind_services`, `process_exit_services`
- `procfs.h`: `proc_stat`, `proc_pid_get_owner`, `proc_readdir`, `proc_meminfo_read`, `proc_ps_read`, `proc_status_read`, `proc_cmdline_read`, `proc_pid_stat_read`, `proc_pid_maps_read`, `proc_pid_statm_read`, `proc_pid_oom_score_adj_read`, `proc_pid_oom_score_adj_write`, `proc_pid_oom_score_read`, `proc_pid_fd_link_target_read`, `proc_pid_exe_link_target_read`, `proc_pid_environ_read`, `proc_resolve_path`, `proc_uptime_read`, `proc_stat_read`, `proc_version_read`, `proc_boot_cmdline_read`, `proc_cpuinfo_read`, `proc_blockdevices_read`, `proc_netinfo_read`, `proc_drivers_read`, `proc_loadavg_read`, `proc_filesystems_read`, `proc_partitions_read`, `proc_interrupts_read`, `proc_iomem_read`, `proc_ioports_read`, `proc_modules_read`, `proc_timer_list_read`, `proc_net_dev_read`, `proc_net_route_read`, `proc_net_tcp_read`, `proc_net_udp_read`, `proc_net_raw_read`, `proc_net_unix_read`, `proc_kmsg_read`, `proc_swaps_read`
- `ps2_mouse.h`: `ps2_mouse_shutdown`, `ps2_mouse_is_available`, `ps2_mouse_get_state`, `ps2_mouse_feed_byte`, `ps2_mouse_set_sample_rate`, `ps2_mouse_packet_available`, `ps2_mouse_read_packet`
- `ps2_mouse_pkt.h`: `ps2_mouse_pkt_reset`, `ps2_mouse_pkt_feed`
- `ps2_set1.h`: `ps2_set1_shift`
- `pseudo_fs.h`: `pseudo_fs_register`, `pseudo_fs_register_dynamic`, `pseudo_fs_lookup`, `pseudo_fs_reset`, `pseudo_fs_read_fd`, `pseudo_fs_open_path`, `pseudo_fs_close_fd`, `pseudo_fs_stat_path`, `pseudo_fs_stat_now`, `pseudo_fs_path_has_children`, `pseudo_fs_collect_registry_children`
- `pty_devfs.h`: `devfs_pty_vfs_slot`
- `ramtab.h`: `ramtab_eq`, `ramtab_basename`, `ramtab_prune_matching`
- `reclaim.h`: `mm_reclaimer_register`, `mm_reclaim_run`
- `resource_registry.h`: `resource_unregister_irq`, `resource_unregister_ioport`, `resource_unregister_mmio`
- `rtc.h`: `rtc_init`, `rtc_read_time`, `rtc_read_register`, `rtc_bcd_to_binary`, `rtc_civil_year`
- `serial_io.h`: `serial_init`, `serial_try_read_char`
- `signal_irq.h`: `irq_frame_sp`
- `signals.h`: `signals_on_sigreturn`, `signals_deliver_from_irq_frame`
- `sock_icmp.h`: `sock_icmp_rx_deliver`
- `sock_stream.h`: `sock_stream_recv`, `sock_stream_buf_space`, `sock_stream_buf_count`
- `sound_blaster.h`: `sb16_set_master_volume`, `sb16_mixer_read`, `sb16_speaker_on`, `sb16_speaker_off`, `sb16_play_pcm`
- `stat_user.h`: `ir0_stat_to_user`
- `swap.h`: `ir0_swap_slot_alloc`, `ir0_swap_slot_get`, `ir0_swap_slot_put`, `ir0_swap_slot_busy`, `ir0_swap_read_page`, `ir0_swap_write_page`, `ir0_swap_out_page`, `ir0_swap_in_page`, `ir0_swap_dup_pte`, `ir0_swap_drop_pte`, `ir0_swap_drain_area`, `ir0_swap_try_reclaim`
- `syscall_table.h`: `syscall_handler_set`, `syscall_context_handler_set`
- `sysfs.h`: `sysfs_stat`, `sys_kernel_hostname_write_reg`, `sys_kernel_version_read_reg`, `sys_kernel_osrelease_read_reg`, `sys_kernel_max_processes_read_reg`, `sys_kernel_max_processes_write_reg`, `sys_kernel_max_processes_limit`, `sys_kernel_panic_read_reg`, `sys_kernel_panic_write_reg`, `sys_console_mode_read_reg`, `sys_devices_system_read_reg`, `sys_devices_cpu_read_reg`, `sys_devices_cpu_online_read_reg`, `sys_devices_cpu_online_write_reg`, `sys_devices_block_read_reg`, `sys_kernel_build_read_reg`, `sys_kernel_features_read_reg`, `sys_kernel_mm_read_reg`, `sys_class_net_list_read_reg`, `sys_class_net_attr_read_reg`, `sys_class_net_attr_read_named`, `sys_class_net_parse_attr`, `sys_class_net_find_name`, `sys_class_net_collect_children`, `sys_class_net_path_has_children`
- `timerfd.h`: `ir0_timerfd_is`, `ir0_timerfd_acquire`, `ir0_timerfd_release`
- `tlb_sync.h`: `mm_tlb_sync_page`, `mm_tlb_sync_all`
- `tls.h`: `tls_set_base`
- `usb_host.h`: `ir0_usb_host_controller_count`, `ir0_usb_host_describe`
- `vbe.h`: `vbe_init_from_boot`, `vbe_init`, `vbe_putchar`, `vbe_get_info`, `vbe_get_pitch`, `vbe_get_fb`, `vbe_get_fb_phys`, `vbe_get_fb_size`, `vbe_is_available`, `vbe_map_supervisor`, `vbe_rgb_to_pixel`
- `vfs_file.h`: `ir0_vfs_file_product`, `ir0_vfs_file_resource_ops`
- `vga.h`: `putchar`, `putchar_at`, `make_color`, `scroll`
- `video_backend.h`: `video_backend_get_fb`, `video_backend_get_pitch`, `video_backend_get_fb_phys`, `video_backend_get_fb_size`
- `wait.h`: `WIFSIGNALED`, `WTERMSIG`
