# ARM64 kernel stack and SP_EL1 (product)

> **Last verified:** 2026-11-07  
> **Source of truth:** `arch/arm64/asm/entry/vectors.S`, `arch/arm64/sources/arch_switch.c`  
> (`set_current_kernel_stack`), `arch/arm64/asm/boot/entry.S`, `sched/switch/context_switch.c`
> (`context_prepare_next` → `set_current_kernel_stack`).

## Why two stack pointers matter

- **SP_EL0**: user stack pointer when the thread runs at EL0.
- **SP_EL1** (banked when `SPSel` is 1): kernel stack pointer used on synchronous  
  exception entry **from** EL0 into EL1. The vector stubs in `vectors.S` subtract  
  space for a fixed trap frame **before** saving GPRs.

The cooperative context switch path (`arm64_cpu_switch` in `switch_early.S`) uses  
the current EL1 stack pointer (`mov sp, x2`), which must match SP_EL1 when the  
kernel runs in EL1h with `SPSel == 1`.

## Exception frame reservation

The lower-EL sync path uses `exc_entry_frame`, which does:

```text
sub sp, sp, #256
```

So the first exception from EL0 into EL1 needs **at least 256 bytes** of free  
stack **below** the SP_EL1 value in use at entry, on a **16-byte** boundary.  
`set_current_kernel_stack` therefore programs:

```text
sp_el1 = (kstack_top - 256) & ~0xF
```

with `sp_el1 >= kstack_base` (else the update is skipped). This is stricter than  
“top − 16”: it leaves room for the vector prologue.  
`IR0_PROC_KSTACK_SIZE` (see `config.h`) is large enough that the check passes for  
normal processes.

The write uses `SPSel=0` so `SP_EL1` is the inactive bank. A direct  
`msr sp_el1` at EL1h would replace the live kernel stack; the function  
epilogue would then pop LR from the empty kstack and return to EL1 address 0.

## Boot: SPSel

Firmware may leave `SPSel` undefined for EL1. The kernel entry stub forces  
**`msr spsel, #1`** so SP_EL1 is the bank used for the boot stack and matches the  
vector model above.

## SVC return from EL0

`arm64_exc_sync_lower` receives a pointer to the 256-byte trap frame. For SVC,  
the handler sets `frame[0]` to the syscall return value; the vector macro then  
restores registers and executes `eret`. No separate `msr sp_el1` runs on that  
return path—the frame was allocated from SP_EL1 at entry.

## Related sysfs

Boot/firmware snapshot (separate topic): [`sysfs-firmware-ir0.md`](sysfs-firmware-ir0.md).
