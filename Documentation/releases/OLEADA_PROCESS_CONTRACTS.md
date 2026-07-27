# Oleada: process gaps + baseline capture + contracts

> **Última verificación:** 2026-07-27  
> **Fuente de verdad:** código citado; gates en informe de oleada

## Documented in this pass

| Tema | Artefacto |
|------|-----------|
| Baseline pins + gaps | `scripts/baseline_capture.py`, `out/baseline/`, [`BASELINE_GAPS.md`](BASELINE_GAPS.md) |
| QEMU capture suite | `python3 scripts/baseline_capture.py --qemu` (uname…find /heart) |
| Networking matrix | [`NETWORKING_MATRIX.md`](NETWORKING_MATRIX.md) |
| Sched vs lifecycle | `process_set_sched_state` / `process_mark_zombie` / `process_lifecycle_t` |
| Fork transactional | parent prepare **after** attach; KTM fault sites in `fork.c` |
| Portable args | `task_get_arg0/1/2` |
| MM address space | `process->mm` sole store; accessors `process_pgd` / `process_heap_*` / `process_mmap_list` |
| Syscall frame | `arch_syscall_frame_t` (alias `syscall_user_frame_t`) |
| TLS | `arch_thread_state_t` union with `fs_base` (ASM offset preserved) |
| Contract suites | arch_task, pseudo_fs, mm_sole_store, netdev, sched_backend, block_backend |

## Host contract suites (§7)

| Suite | File |
|-------|------|
| arch_task_contract | `tests/host/test_arch_task_contract.c` |
| pseudo_fs_contract | `tests/host/test_pseudo_fs_contract.c` |
| mm_mirror_contract (sole-store) | `tests/host/test_mm_mirror_contract.c` |
| netdev_contract | `tests/host/test_netdev_contract.c` |
| sched_backend_contract | `tests/host/test_sched_backend_contract.c` |
| block_backend_contract | `tests/host/test_block_backend_contract.c` |
| (existing) vfs / blockdev facade / ktm poll | `test_vfs_backend_contract.c`, `test_blockdev_facade.c`, `test_ktm_sched_contract.c` |

## Still not claimed

- SMP-safe PMM (irq_save only)
- Full ARM64 ISD userspace
- BusyBox net applets as “supported” without smoke PASS
