<!-- IR0 AI dev rule: ir0-userspace-monolith-debt -->
<!-- alwaysApply: true -->
<!-- description: IR0 userspace path — monolith split targets, facade rules, no syscalls.c bloat -->

# IR0 Userspace Architecture Constraints

Apply to all fixes toward real userspace (init, musl, BusyBox, syscalls, exec, readback).

## Monolith split (debt — do not expand)

- `kernel/syscalls.c` (~4800 lines) must shrink over time, not grow.
- Target layout (incremental, not big-bang):
  - `kernel/syscalls/fs_syscalls.c`
  - `kernel/syscalls/process_syscalls.c`
  - `kernel/syscalls/mm_syscalls.c`
  - `kernel/syscalls/io_syscalls.c`
  - `kernel/syscalls/time_syscalls.c`
  - `kernel/syscalls/syscall_dispatch.c`
- **New logic**: prefer small helpers in `includes/ir0/*` or new syscall submodules; avoid adding blocks to `syscalls.c`.
- If a fix must touch `syscalls.c`, note `ARCH_DEBT: move to syscalls/<area>.c` in the commit/summary.

## Scheduler / context switch

- Tend toward `kernel/sched/` (`core.c`, `context_switch.c`, `wait.c`, `sleep.c`).
- Arch backend only in `arch/x86_64/switch_x64.asm` + arch facade.

## Facade rule (mandatory)

Subsystems communicate via callbacks/facades, not hard cross-deps:

| Path | Facade |
|------|--------|
| open/read/write | `linux_open_flags_to_ir0` → `vfs_*` → `fs->ops` |
| mmap/brk | mm facade → arch paging |
| schedule/switch | sched core → arch context switch |
| user copy | `copy_user` / `copy_*_region_in_directory` with explicit `page_directory` |

**Never** assume `active_cr3` when the target is another process (pipes, wake, cross-task copy).

## Generic fixes (current baseline)

- Linux open flags: translate in syscall/ABI layer only (`includes/ir0/open_flags.*`).
- VFS: generic create/truncate semantics; backends implement path ops only.
- MINIX/tmpfs/etc.: backend only; no Linux ABI flags.
- User copies: `copy_to_user_region_in_directory(pml4, …)` when CR3 may differ.

## Oleada discipline

- No massive refactor unless user approves a split oleada.
- Continue vertical slices (e.g. cat/readback) with minimal diffs + CTR gates.
