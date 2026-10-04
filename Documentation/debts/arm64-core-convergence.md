# ARM64 to common-core convergence manifest

Status: active, 2026-10-03. This is a dependency manifest, not an instruction
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
  `arch_task_ops.c`. The redundant `arch_task_ops.h` alias was removed.
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
- `arch/arm64/sources/{board,boot_info,platform,pl011,serial_io_arm64}.c`;
  owner: platform discovery and early console; destination: keep, later split
  into generic ARM platform plus QEMU/Raspberry Pi providers; DTB-derived
  resources are the interface boundary.

### Early scaffolding to replace, not generalize

- `arch/arm64/sources/syscall_{mm,vfs,time,signal,process,io}_early.c`;
  owner: bring-up adapters; destination: delete after the matching common
  syscall handlers link and pass BusyBox/init; dependencies are the semantic
  syscall-provider table and user-copy/address-space facades.
- `arch/arm64/sources/syscall_early.c`; owner: early SVC coordinator;
  destination: delete after production dispatch owns the EL0 path; it may only
  decode, compose providers and invoke the semantic table.
- `arch/arm64/sources/{process,switch,rr}_early.c` and `switch_early.S`;
  owner: bring-up scheduler/process harness; destination: delete after common
  create/fork/exec/exit/wait plus production ARM switch reach real PID 1.
- `arch/arm64/sources/{elf_image,elf_load,busybox_load}_early.c`;
  owner: embedded-image bring-up provider; destination: delete after common
  exec/VFS loads `/init`; the ELF parser itself already lives in common code.
- `arch/arm64/sources/{rootfs,init_handoff}_early.c`; owner: embedded rootfs and
  PID 1 adapter; destination: delete after common rootfs/VFS and process spawn
  implement the existing init-handoff operations.
- `arch/arm64/sources/{virtio_blk,virtio_net}_early.c`; owner: bring-up device
  providers; destination: delete after the common block/network stacks consume
  production virtio transport callbacks.
- `arch/arm64/sources/{mmu,exc}_early.c`; owner: pre-production MMU/exception
  bootstrap; destination: retain only the truly pre-MMU entry portion and
  delete the runtime portion once production MM/fault handling takes over.
- `arch/arm64/sources/{freestanding,min_link,rr_early}_stubs.c` and
  `irq_portable_stubs.c`; owner: link/test scaffolding; destination: test mocks
  or deletion; forbidden as providers in a production ARM image.

### Boot-only assets and harnesses

- `boot_entry.S`, `vectors.S`, `boot_stub.c`, `arch_early.c`;
  owner: ARM boot; destination: keep, later `arch/arm64/boot/`; `boot_stub.c`
  must shrink as early subsystems are retired.
- `hello_embed.S`, `busybox_embed.S`, `slice_hello.c`, `all_objs_mark.c`;
  owner: ARM integration tests; destination: an ARM test-image area after the
  production boot no longer embeds them; they are not kernel facilities.
- `board_boot_min.c`; owner: board contract harness; destination: ARM tests;
  must never be linked into the product image.

## Ordered convergence

1. Link common process core with ARM task/frame/debug providers. **Done for the
   aggregate ARM boot:** registry, core, signal-enter, wait-state, domain state,
   saved context/environment, pseudo-FD binding and fork/TLS preparation are
   strong symbols; the smoke rejects fallback process stubs.
2. Link the common MM lifecycle. **Done for the aggregate ARM boot:** common
   address-space creation, refcount, table reclaim and VMA lifecycle are strong
   symbols. Close create, then fd/VFS dependencies needed by exec/exit/wait,
   without adding fallback stubs.
3. Replace early process and syscall providers one subsystem at a time.
4. Load PID 1 through common VFS/exec and retire embedded-rootfs policy.
5. Only then perform pure directory moves for boot, MM, IRQ and platform code.

The acceptance condition is stronger than compilation: x86 production boot,
ARM QEMU BusyBox/init EL0, Raspberry Pi 3 userspace, Raspberry Pi 5 compile,
architecture guards and the minimal configuration matrix must stay green.
