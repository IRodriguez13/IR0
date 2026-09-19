<!-- IR0 AI dev rule: ir0-ktm-instrumentation -->
<!-- alwaysApply: false -->
<!-- description: When and how to instrument IR0 with KTM without perturbing what you measure -->

# KTM — instrumentation limits

KTM earns its place on **ordering and lifetime** bugs across processes, where
`printf` is unreliable: interleaving, loss, and `klog_*_fmt` silently dropping
arguments past the eighth. Typed events do not have those failure modes.

## Where you may emit directly

`ktm_event_emit4` is safe in process context and in ordinary syscall paths
(pipe read/write, fork, exec, scheduler state changes via
`process_sched_state_trace`).

## Where you may NOT emit — record instead

**Never call `ktm_event_emit4` from the context switch**, from a path holding
IRQs off in a hot loop, or from any code running while the kernel stack or CR3
is mid-transition.

Measured: emitting from the resume gate in `arch_switch.c` took
`smoke-pipeline-stress` from 1 failure in 6 to 2 in 4 and introduced SIGSEGV
modes that did not exist before. Removing it restored the baseline. The
instrumentation was changing the ordering it was meant to observe.

Use `ktm_deferred_record()` (`includes/ir0/ktm/deferred.h`) there: plain stores
into a fixed array, no calls, no locks, no formatting. `ktm_deferred_flush()`
converts the rows to real events from the dump ioctl, so both appear in one
timeline.

## Reading a failure

Wire `KTM_IOC_DUMP_EVENTS` into the smoke's failure path, not into its success
path — the point is the window before the failure. Filter by subsystem with
`KTM_DUMP_ARG(mask, count)`; a timeout spends its whole window on BLOCK/WAKE
and will bury everything else if IPC and SCHED share one budget.

Absence of an expected event is evidence. The pipe EOF race was found exactly
that way: `PIPE_WRITE` followed by no `PIPE_READ`, then `END_CLOSE` with bytes
still buffered.

## Fault injection

`KTM_FAULT_HIT("site.name")` plus `KTM_IOC_CONFIG_FAULT` is the only way to
exercise kernel error paths that never run under normal load (`fork_kstack`,
`fork_cow`, `kmem.alloc`, `sock.create`). Prefer adding a hook over adding a
test that cannot reach the branch. Pair every new hook with a case asserting
that the failure leaves no leaked resource (`kernel-resource-lifecycle`).
