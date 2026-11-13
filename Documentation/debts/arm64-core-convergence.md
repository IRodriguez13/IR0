# ARM64 to common-core convergence manifest

Status: active, 2026-10-18. This is a dependency manifest, not an instruction
to move files mechanically. A source moves only when its owner is proven and
the existing x86 and ARM runtime gates remain green.

## Rules

- Common code owns policy, validation, ordering and Linux ABI semantics.
- An ISA backend owns only register formats, exception entry/return,
  translation encoding, interrupt-controller access and platform discovery.
- A facade exists only when at least one common caller needs an ISA mechanism.
- There is one canonical public contract. Include-only aliases are deleted.
- Early ARM code is replacement scaffolding. It must not become a second core.
- Moving a file and changing behavior are separate signed commits.

## Current canonical contracts

- Task context: `includes/ir0/task_ops.h`; implemented by each ISA's
  `task.c`. The redundant `arch_task_ops.h` alias was removed.
- Syscall frame: `includes/ir0/syscall_frame.h`; ISA storage stays in
  `arch_syscall_frame_*.h`, and each ISA implements frame transfer.
- Address translation: `includes/ir0/arch_mm.h`; common MM consumes semantic
  mapping flags and opaque address-space roots.
- ELF image loading: `includes/ir0/elf64_image.h`; common code validates and
  sequences segments, while the backend maps/copies/zeros the target image.
- Init handoff: `includes/ir0/init_handoff.h`; common code owns ordering and
  error propagation, while the current ARM early provider supplies mechanisms.
- Debug state: `includes/ir0/arch_debug.h`; common process code calls the
  semantic `debug_state_init()` operation.
- Thread-local storage: `includes/ir0/tls.h`; common process and syscall code
  use semantic TLS operations, while each ISA owns its thread-pointer register.
- Descriptor resources: `includes/ir0/fd_resource.h`; common process code owns
  descriptor-table policy and rollback, while each provider owns acquire and
  release semantics. The current legacy adapter is a migration boundary and
  must shrink as providers publish their operations directly.

## File manifest

Each entry records current location; owner; eventual location; dependencies or
exit condition. `keep` means the current path is acceptable until a later
pure-move pass.

### Production ISA backends

- `arch/arm64/sources/{arch_task_ops,arch_syscall_frame,arch_signal}.c`;
  owner: ARM task/signal mechanism; destination: keep, later `arch/arm64/task/`;
  depends on the common task, syscall-frame and signal contracts only.
- `arch/arm64/sources/{arch_mm,mm_ops,arch_page_fault,arch_pf_debug}.c`;
  owner: ARM translation mechanism; destination: keep, later `arch/arm64/mm/`;
  must not acquire VMA, allocator or exec policy.
- `arch/arm64/sources/{arch_switch,arch_fork,fork_asm_hooks,task_stack}.c`;
  owner: ARM context mechanism; destination: keep, later `arch/arm64/task/`;
  common process lifecycle must call it through semantic task/fork contracts.
- `arch/arm64/sources/{syscall_decode,elf_policy,arch_debug}.c`;
  owner: small ARM policy/mechanism providers; destination: keep; no early
  runtime dependency is allowed.
- `arch/arm64/sources/{timer,gic_v2,bcm2836_irq,irq_backend,interrupts}.c`;
  owner: ARM timer/IRQ mechanism; destination: keep, later `arch/arm64/irq/`;
  board selection must remain behind platform discovery.
- `arch/arm64/sources/{board,boot_info,platform,pl011,serial}.c`;
  owner: platform discovery and early console; destination: keep, later split
  into generic ARM platform plus QEMU/Raspberry Pi providers; DTB-derived
  resources are the interface boundary.

### Common staged adapters (fold in progress)

- `fs/ramtab.c` + `includes/ir0/ramtab.h`; owner: in-memory names without a
  VFS mount; destination: keep until every staged path has a mount; then
  delete callers in `erfs_*.c`.
- `includes/ir0/bpath.h`; owner: optional bind from the staged adapter
  to product `vfs_*`; destination: keep as the transition seam.
- `ipc/early_anonfd.c`, `ipc/early_unix.c`; owner: staged IPC object tables;
  destination: product `ipc/` / pipe objects. Keep out of `fs/`.
- `kernel/syscalls/erfs_*.c`; owner: `/`, `/init`, `/dev/*`, CPIO,
  syscall routing onto those tables; destination: shrink only. `/tmp/*`
  uses product VFS when bound (`ARM64_EARLY_PATH_FOLD_OK`).
- `ir0_product_identity_provider` / `ir0_product_cwd_provider`; owner:
  historical fold-slice markers (retired with the freestanding stack); product
  ABI proof is `syscall_dispatch` + `ir0-selftest-arm64` / `identity`.
- `kernel/syscalls/early_{dispatch,vfs,io,time,mm,process}.c`; owner: staged
  freestanding EL0 only; the freestanding stack is gone, so nothing calls them.
  Destination: delete. See [`../EARLY_FOLD.md`](../EARLY_FOLD.md).

### Early scaffolding still linked by the product image

The freestanding bring-up stack (`mmu_early`, `syscall_early`,
`{process,rr}_early`, `{elf_image,elf_load,embedded_elf}_early`,
`{rootfs,init_handoff}_early`, `usercopy_early`, `virtio_net_early`,
`boot_stub`, `slice_hello`, `syscall_stub`, `all_objs_mark`, the hello
embeds and the `el0_entry` workload in `vectors.S`) was deleted on
2026-11-07. No build target linked it after product EL0 moved to
`syscall_dispatch` and user roots moved to the common 48-bit walker
(`mmu.c`). What remains:

- `arch/arm64/sources/exc_early.c`; product vectors' C side (SVC dispatch,
  aborts into `mm_page_fault_handle`). Destination: rename out of "early".
- `arch/arm64/sources/switch_early.c` + `switch_early.S`;
  `arm64_cpu_switch` / `arm64_cpu_switch_mm` for the product switch.
  Destination: rename out of "early".
- `arch/arm64/sources/virtio_blk_early.c`; product CPIO/`vda` block provider.
  Destination: the common virtio block driver over `drivers/virtio/virtio_mmio.c`.
- `irq_portable_stubs.c`; compile probe only; forbidden as a product provider.

### Boot-only assets and harnesses

- `entry.S`, `vectors.S`, `early.c`; owner: ARM boot; destination: keep,
  later `arch/arm64/boot/`.
- The board `-min` images (`board_boot_min.c`, `entry_board_min.S`,
  `freestanding_stubs.c`, `linker_rpi.ld`) and `IR0_FREESTANDING_BOOT` were
  retired on 2026-11-12; `make ir0 arm64 <board>` builds the product link.

## Ordered convergence

1. Link common process core with ARM task/frame/debug providers. **Done for the
   aggregate ARM boot:** registry, core, signal-enter, wait-state, domain state,
   saved context/environment, pseudo-FD binding and fork/TLS preparation are
   strong symbols; the smoke rejects fallback process stubs.
2. Link the common MM lifecycle. **Done for the aggregate ARM boot:** common
   address-space creation, refcount, table reclaim and VMA lifecycle are strong
   symbols.
3. Link common process creation and fork. **Done for the aggregate ARM boot:** `spawn`,
   credentials/group initialization, files lifecycle, initial fd table and the
   ARM task-stack provider are linked as strong symbols. Common `fork` now uses
   the provider-neutral descriptor-resource lifecycle and deterministic partial
   rollback. Close the remaining VFS dependencies needed by common
   `exec` without adding fallback subsystem stubs. Close the remaining
   scheduler, signal, IPC and console dependencies required by common
   `exit`/`wait` next.
4. Replace early process and syscall providers one subsystem at a time.
   **Started:** path table is `fs/ramtab.c`; ARM ALL `/tmp` uses product VFS.
   **Product SVC:** `syscall_dispatch` is linked and used; bring-up
   `syscall_early` / hello / RR are filtered out of `kernel-arm64-product.bin`.
   Next: shrink portable `early_process.c` / erfs for remaining staged nodes.
5. Load PID 1 through common VFS/exec. **Product:** no embedded hello seed;
   CPIO/`vda` bind + `kexecve("/sbin/init")`. ISD AArch64 rootfs still open.
6. Board descriptor lives under `arch/arm64/include/arm64_board.h` (not
   `includes/ir0/`). `/proc/cmdline` uses `boot_memory_cmdline()`.
7. Only then perform pure directory moves for boot, MM, IRQ and platform code.

The acceptance condition is stronger than compilation: x86 production boot,
ARM QEMU BusyBox/init EL0, Raspberry Pi 3 userspace, Raspberry Pi 5 compile,
architecture guards and the minimal configuration matrix must stay green.
