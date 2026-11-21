# Behavior on `dev` (2026-11-20)

> **Last verified:** 2026-11-20
> **Source of truth:** `dev` at `93bccdf8`. Code and selftests cited below.
> Spanish: [`esp/ABI_DEV.md`](esp/ABI_DEV.md).

Integration onto `dev` runs the full gate battery (host tests, `arch-guard`,
`kernel-tests`, conformance, the selftests on x86-64 and arm64, cow/fork-storm,
and the fuzz seeds) on the tree that already contains the change. A selftest
that is green only on its feature branch is not enough.

| Behavior | Commit | Contract |
|---|---|---|
| Growable fd table | `9d3ceb58` | Slots grow by `files_grow()` up to `RLIMIT_NOFILE`, capped at `IR0_NOFILE_CEILING` (4096). Soft default 1024. One installer: `fd_alloc_lowest()` (`includes/ir0/files_struct.h`, `includes/ir0/fd_types.h`). Selftest `nofile`. |
| Lazy anonymous commit | `de8cafef` | Anonymous `PROT_NONE` and `MAP_NORESERVE` (`0x4000`) do not populate PTEs (`kernel/syscalls/mm_syscalls.c`). A read-write anonymous mapping without `MAP_NORESERVE` is populated immediately. Selftest `vm_commit`. |
| Linux `ucontext` | `921a2121` | The handler edits the `ucontext` inside the existing sigframe. `rt_sigreturn` imports it. arm64 stores one `mcontext` (the `sigcontext`); x86-64 keeps a private `sigcontext` plus the 936-byte `ucontext`. Selftest `ucontext`. |
| Hypervisor kind | `bf4fbb05` | `hypervisor_kind()` is the probe (`includes/ir0/cpu_info.h`). It returns `-1` when the probe did not run; that is not bare metal. x86 uses CPUID leaf `0x40000000`. arm64 uses the SMCCC UID for KVM and an FDT `xen,xen` node for Xen. `/proc/cpuinfo` prints `hypervisor_kind`. `hypervisor_present()` is the portable predicate on top of that probe. Selftest `hypervisor_kind`. |
| `accept4` peer address | `89ceacc8` | For an `AF_UNIX` child, `sys_accept4` writes the peer name with `unix_fill_sockaddr`, the same bytes as `getpeername` (`kernel/syscalls/socket_syscalls.c`). A `NULL` address skips the copy. A failed copy closes the new fd. Selftest `accept4`. |
| `CLONE_SIGHAND` | `93bccdf8` | The flag shares one `sighand_struct`. Without `CLONE_VM`, `sys_clone` returns `-EINVAL`. With `CLONE_VM` and without `CLONE_THREAD`, the child shares the parent's `mm`. `SIG_IGN` updates every sharer. `exec` copies the table when its refcount is greater than one (`process_signals_unshare`). Selftest `sighand`. |

`hypervisor_vendor()` stays per ISA: x86 reads the CPUID vendor leaf, arm64
does not invent an x86 string.
