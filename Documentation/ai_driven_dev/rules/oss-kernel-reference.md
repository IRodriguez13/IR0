<!-- IR0 AI dev rule: oss-kernel-reference -->
<!-- alwaysApply: false -->
<!-- description: Use Linux/BSD and major OSS kernels as reference for MM, syscalls, and serious bugs -->

# Open-source kernel reference (Linux, BSD, others)

When fixing **memory/paging faults**, **syscall/ring-3 bring-up**, **scheduler/context-switch bugs**, or **serious design errors**, cross-check established kernels. Adapt ideas and invariants — do not paste code verbatim; keep IR0 facades and licensing clean.

## When to use this rule

- Page faults with CR2 near user entry, NX/huge-page walks, CR3 not loaded before `iretq`/`sysret`
- ELF/spawn ordering (runqueue before segments mapped)
- `copy_to_user` / user buffer access without correct CR3 or PFN extraction
- Syscall entry/exit ABI mismatches (Linux `syscall`/`sysret` vs `int 0x80`)
- Locking/IRQ races, use-after-free in process or VFS paths

## Memory management (x86-64)

| Topic | Linux | FreeBSD | NetBSD/OpenBSD |
|-------|-------|---------|----------------|
| PFN mask | `PTE_PFN_MASK` in `pgtable.h` | `VM_PAGE_TO_PHYS` / pmap PTE bits | `PTE_ADDR` / `pmap_pte_p` |
| Large pages | `pmd_large()` / `pud_large()` | `pmap_is_prefaultable`, 2M/1G in pmap | `PTE_PS` checks in pmap walk |
| User bit all levels | `vm_get_page_prot`, `_PAGE_USER` on tables | `VM_PROT_READ` in pmap_enter | `PROT_*` propagated in pmap |
| Zero anonymous | `clear_highpage()` | `pmap_zero_page` | `pagezero` / pmap zero |
| Copy to user mm | `copy_to_user`, `access_process_vm` | `copyin`/`copyout`, `uiomove` | `copyin`/`copyout` |
| Switch mm | `switch_mm()` → CR3 load | `pmap_activate` | `pmap_activate` |

**IR0 invariants (from upstream practice):**

- Mask table/leaf addresses with **full PFN mask** (`0x000FFFFFFFFFF000` on x86-64), not `~PAGE_MASK` alone — NX (bit 63) must not leak into pointers.
- Never descend into a **leaf/huge** entry as if it were a page table.
- Non-leaf entries: present+rw (+ user for user mappings); **no NX** on table levels.
- Map/populate page tables under **kernel CR3**; load **process CR3** before first user `iretq`/`sysret` (Linux `switch_mm`, BSD `pmap_activate`).
- Do not inherit boot **2 MiB identity** leaves into a fresh user mm when they overlap static ELF load (`0x400000`).

## Process, ELF, scheduler

- **Linux** `fs/binfmt_elf.c` — `load_elf_binary` completes mappings/stack before user thread runs.
- **FreeBSD** `sys/kern/imgact_elf.c` — image activation vs thread start ordering.
- **NetBSD** `sys/kern/kern_exec.c` — exec path and pmap setup before user return.
- **Linux** `arch/x86/kernel/process.c` — `context_switch` + `switch_mm`.
- **FreeBSD/NetBSD** `mi_switch`, `pmap_activate` on thread switch.

Do not `sched_add_process()` until PT_LOAD, stack, and arch entry state are ready.

## Syscalls & ring 3

- **Linux** `arch/x86/entry/syscall_64.S`, STAR/LSTAR/SFMASK MSRs, `sysret` vs `iretq`.
- **FreeBSD** `syscall`/`sysenter` paths in `sys/i386` or `sys/amd64`.
- Compare argument registers (Linux: rax, rdi, rsi, rdx, r10, r8, r9) with IR0 dispatch.

## Useful entry points (browse upstream trees)

**Linux:** `arch/x86/mm/pgtable.c`, `mm/memory.c`, `include/asm-generic/pgtable.h`  
**FreeBSD:** `sys/amd64/amd64/pmap.c`, `sys/kern/kern_syscalls.c`  
**NetBSD:** `sys/arch/x86/x86/pmap.c`, `sys/kern/kern_exec.c`  
**OpenBSD:** `sys/arch/amd64/amd64/pmap.c`, `sys/kern/kern_exec.c`

## Workflow

1. Reproduce and note CR2/CR3/RIP and whether fault is user or supervisor.
2. **Holistic pass:** trace the full dependency chain for the failing scenario (not only the crashing frame). Example for userspace init smoke:
   boot → driver init → VFS/disk → `kexecve` → page tables → scheduler → CR3/`iretq` → user `_start` → `syscall` → dispatch → FD/console → serial output.
   List each hop and verify it is wired; one missing link (e.g. no CR3 load, empty FD table, wrong STAR for `sysret`) fails the whole path silently.
3. Identify subsystem (mm, exec, sched, syscall, driver, fs).
4. Find the analogous upstream function and compare **ordering and invariants**, not line-by-line code.
5. Fix IR0 minimally; run `make smoke-userspace-init` / ktests as appropriate.
