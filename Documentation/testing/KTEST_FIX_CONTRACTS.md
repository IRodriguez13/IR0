# Guest contracts locked by ktest

> **Last verified:** 2026-11-05
> **Source of truth:** `kernel/test/test_fix_contracts.c`,
> `kernel/test/test_mm_contracts.c`, `kernel/test/test_debug_contracts.c`,
> `kernel/test/test_runner.c`,
> `includes/ir0/futex.h`, `includes/ir0/ipc.h`, `includes/ir0/arch_mm.h`,
> `kernel/futex.c`, `ipc/ipc.c`, `mm/page_fault.c`, `mm/paging.c`,
> `arch/x86-64/sources/mm.c`, `arch/arm64/sources/mm.c`

Spanish: [`../esp/testing/KTEST_FIX_CONTRACTS.md`](../esp/testing/KTEST_FIX_CONTRACTS.md).

`make kernel-tests` boots `kernel-x64-test.bin` and reports through the KTM scenario
`kernel.ktest`. On 2026-11-05 the suite printed `All 84 test(s) passed` and
`TEST_END|kernel.ktest|PASS`. The later panic `Failed to load /sbin/init` is
the test ISO, after that verdict.

Sixteen of those cases live in `kernel/test/test_fix_contracts.c`. They fail
the boot if the behavior below regresses. The COW cases below live in
`kernel/test/test_mm_contracts.c`. The rest of the suite is older and is not
listed here.

## How to run

```bash
make kernel-tests
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
| `read_efault_keeps_offset` | A `USER_MODE` `sys_read` into a range-valid but unmapped user address returns `-EFAULT` without advancing the file offset. The next read returns the two bytes that were written (`AB`). |
| `fsync_null_fd_table` | Poisoning `files_struct.magic` makes `sys_fstat`, `sys_fsync`, and `sys_ftruncate` return `-EBADF`. The magic is restored. |
| `rename_keeps_unlink_errno` | `sys_rename` of one file onto another on a read-only tmpfs returns `-EROFS`. |
| `pipe_read_efault_keeps_bytes` | With the write end closed, a `USER_MODE` `sys_read` into an unmapped user address returns `-EFAULT`. The next read returns the two bytes that were written (`AB`). |
| `socket_read_efault_keeps_bytes` | A `USER_MODE` `sys_read` on an `AF_UNIX` stream into an unmapped user address returns `-EFAULT`. The next read returns the two bytes that were sent, so the failed copy did not dequeue them. |
| `rt_sigsuspend_consumes_pending` | With `SIGUSR1` pending and a handler that is neither `SIG_DFL`, `SIG_IGN`, nor a user address, `sys_rt_sigsuspend` returns `-EINTR`, clears that pending bit, restores the previous mask, and leaves `signal_mask_saved_valid` clear. |
| `futex_interrupted_wait_frees_slot` | A wait that is marked interrupted before sleep returns `-EINTR`. A later `ir0_futex_wake` of that word returns `0` (the slot is free). |
| `futex_exit_drop_frees_slot` | `ir0_futex_drop_process` during the wait frees the slot. The wake issued from the hook returns `0`, and a wake after the wait also returns `0`. |
| `ipc_read_rechecks_after_enqueue` | A channel with one writer and an empty ring still delivers a byte written after the waiter is queued. The task stays `PROCESS_READY`. |
| `unmap_splits_2mb_leaf` | On a private page directory (not the live CR3), unmapping the base of a 2 MiB identity leaf clears that 4 KiB page and leaves the next 4 KiB page mapped. |
| `map_replaced_frame_is_released` | Replacing a PTE with a different frame drops the old frame's refcount to 0. Rewriting the same frame leaves the refcount at 1. Unmap drops it to 0. |
| `map_fixed_aborts_when_unmap_fails` | `MAP_FIXED` over two anonymous pages returns `-ENOMEM` when the unmap backend refuses the second page. The first page is no longer mapped. The second page's PTE value is unchanged. |
| `shmget_metadata_oom_returns_enomem` | With `kmem.alloc` armed to fail once, `sys_shmget` of a private 4 KiB segment returns `-ENOMEM` instead of publishing a half-initialized segment. |

The COW cases are new and live in `kernel/test/test_mm_contracts.c`:

| KTAP name | What a pass means |
|-----------|-------------------|
| `cow_write_fault_drops_old_once` | A write fault on a `PAGE_COW` PTE replaces the frame and drops the old frame exactly once (no `PMM: double free detected` panic). The new PTE is writable without `PAGE_COW`, and the page contents survive the copy. |
| `copy_to_user_cow_break_drops_old_once` | A `copy_to_user_mm` write to a `PAGE_COW` page breaks COW through the uaccess path and drops the old frame exactly once (no double free). The new PTE is writable without `PAGE_COW`, and the written bytes read back through `copy_from_user_mm`. |

The block-device case lives in `kernel/test/test_debug_contracts.c`:

| KTAP name | What a pass means |
|-----------|-------------------|
| `devfs_disk_unaligned_contract` | `/dev/hd*` keeps the byte offset inside a sector. A read at offset 1 returns bytes 1..512; a 4-byte write at offset 3 changes only those bytes; a 600-byte write at offset 500 changes only that range. Reads clamp to the device extent. |
| `devfs_disk_ioctl_bounds_contract` | Legacy `DISK_*` ioctls on a partition reject sector numbers past the partition extent, never truncate the absolute LBA into the disk start, fail closed when the partition entry cannot be resolved, and saturate geometry bytes instead of wrapping. |

A double free panics the guest before the suite prints its verdict, so a
regression fails the whole `make kernel-tests` run.

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
- The 2 MiB case uses a private directory. It does not unmap a 1 GiB identity
  leaf in the live CR3. That leaf still cannot be split by
  `map_page_in_directory`.
- Passing `make kernel-tests` is the x86-64 guest proof. It is not an ARM64 boot.
