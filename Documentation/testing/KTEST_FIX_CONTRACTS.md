# Guest contracts locked by ktest

> **Last verified:** 2026-11-02
> **Source of truth:** `kernel/test/test_fix_contracts.c`,
> `kernel/test/test_runner.c`, `includes/ir0/futex.h`, `includes/ir0/ipc.h`,
> `includes/ir0/arch_mm.h`, `kernel/sync/futex.c`, `ipc/ipc.c`,
> `arch/x86-64/sources/arch_mm.c`, `arch/arm64/sources/arch_mm.c`

Spanish: [`../esp/testing/KTEST_FIX_CONTRACTS.md`](../esp/testing/KTEST_FIX_CONTRACTS.md).

`make ktest` boots `kernel-x64-test.bin` and reports through the KTM scenario
`kernel.ktest`. On 2026-11-02 the suite printed `All 76 test(s) passed` and
`TEST_END|kernel.ktest|PASS`. The later panic `Failed to load /sbin/init` is
the test ISO, after that verdict.

Thirteen of those cases live in `kernel/test/test_fix_contracts.c`. They fail
the boot if the behavior below regresses. The other cases are older and are
not listed here.

## How to run

```bash
make ktest
```

The runner is `scripts/make/ktm.mk` (`SMOKE_QEMU_RUN`, 128 MiB guest, 60 s
timeout). The log is copied under `$TMPDIR` or `/tmp` and may contain NUL
bytes; search it with `rg -a`.

## Cases

| KTAP name | What a pass means |
|-----------|-------------------|
| `readlink_returns_copied_bytes` | `sys_readlink` of `/proc/self/fd/0` (or `/proc/self/exe` when that link is shorter than 2 bytes) returns the copied length. A 1-byte buffer returns `1`. |
| `fstat_redirected_stdio_is_file` | After `dup2` of a tmpfs regular file onto fd 0, `sys_fstat` fills `struct ir0_user_stat` (`includes/ir0/stat_user.h`) and the mode is a regular file, not a character device. |
| `lseek_redirected_stdio_uses_file` | After `dup2` of a 4-byte file onto fd 1, `sys_lseek(1, 0, SEEK_END)` returns `4`. `dup2` returns the new fd. |
| `fsync_null_fd_table` | Poisoning `files_struct.magic` makes `sys_fstat`, `sys_fsync`, and `sys_ftruncate` return `-EBADF`. The magic is restored. |
| `rename_keeps_unlink_errno` | `sys_rename` of one file onto another on a read-only tmpfs returns `-EROFS`. |
| `pipe_read_efault_keeps_bytes` | With the write end closed, a `USER_MODE` `sys_read` into an unmapped user address returns `-EFAULT`. The next read returns the two bytes that were written (`AB`). |
| `rt_sigsuspend_consumes_pending` | With `SIGUSR1` pending and a handler that is neither `SIG_DFL`, `SIG_IGN`, nor a user address, `sys_rt_sigsuspend` returns `-EINTR`, clears that pending bit, restores the previous mask, and leaves `signal_mask_saved_valid` clear. |
| `futex_interrupted_wait_frees_slot` | A wait that is marked interrupted before sleep returns `-EINTR`. A later `ir0_futex_wake` of that word returns `0` (the slot is free). |
| `futex_exit_drop_frees_slot` | `ir0_futex_drop_process` during the wait frees the slot. The wake issued from the hook returns `0`, and a wake after the wait also returns `0`. |
| `ipc_read_rechecks_after_enqueue` | A channel with one writer and an empty ring still delivers a byte written after the waiter is queued. The task stays `PROCESS_READY`. |
| `unmap_splits_2mb_leaf` | On a private page directory (not the live CR3), unmapping the base of a 2 MiB identity leaf clears that 4 KiB page and leaves the next 4 KiB page mapped. |
| `map_replaced_frame_is_released` | Replacing a PTE with a different frame drops the old frame's refcount to 0. Rewriting the same frame leaves the refcount at 1. Unmap drops it to 0. |
| `map_fixed_aborts_when_unmap_fails` | `MAP_FIXED` over two anonymous pages returns `-ENOMEM` when the unmap backend refuses the second page. The first page is no longer mapped. The second page's PTE value is unchanged. |

## Test hooks

The boot task is the only runnable task. A real sleep would hang the suite,
and planting a huge leaf in the live CR3 is not a safe fixture. Three
function pointers stay `NULL` unless a test sets them. Production paths do
not call through a non-NULL hook.

| Symbol | Where it runs | Contract |
|--------|---------------|----------|
| `ir0_futex_before_sleep` | Start of a blocked `ir0_futex_wait`, before `sched_schedule_next` | May mark the task `PROCESS_READY`. Must not call `ir0_futex_wait`. |
| `ipc_sleep_recheck_probe` | After `wait_queue_add`, before the ring is checked again | Must not sleep or schedule. |
| `mm_backend_unmap_hook` | First thing in `mm_backend_unmap_user_page` on x86-64 and ARM64 | Return `-EOPNOTSUPP` to fall through to the ISA backend. Any other value is the unmap result and the generic walker does not run. |

`unmap_page_in_directory` already returns a backend result other than
`-EOPNOTSUPP` without walking the page tables. The `MAP_FIXED` case uses
that: the hook returns `-EIO` for one address and `-EOPNOTSUPP` for the
rest. `is_page_mapped_in_directory` does not consult the hook, so the page
still looks mapped and `mm_prepare_map_fixed` records the failure.

When the hook is `NULL`, x86-64 `mm_backend_unmap_user_page` still returns
`-EOPNOTSUPP`. ARM64 still unmaps only a root it owns and otherwise returns
`-EOPNOTSUPP`.

## What this suite does not claim

- `rt_sigsuspend` does not build a user signal frame. The ktest task has no
  user instruction pointer. The case proves that delivery under the temporary
  mask clears the pending bit and restores the previous mask when
  `process_signal_enter_pending` stays clear.
- AF_UNIX `read` still consumes bytes before `copy_to_user` and has no unread
  path (`kernel/syscalls/fs_syscalls.c`). The pipe case does not cover it.
- The 2 MiB case uses a private directory. It does not unmap a 1 GiB identity
  leaf in the live CR3. That leaf still cannot be split by
  `map_page_in_directory`.
- Passing `make ktest` is the x86-64 guest proof. It is not an ARM64 boot.
