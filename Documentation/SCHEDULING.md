# IR0 Scheduling

IR0 scheduling is selected through a scheduler API with policy configured from
Kconfig.

## Selection Model

- Dispatch layer: `kernel/scheduler_api.c`.
- Policy selection key: `CONFIG_SCHEDULER_POLICY`.
- Current policies wired:
  - Round-robin path.
  - CFS-compatible wrapper path (currently conservative).
  - Priority-compatible wrapper path (currently conservative).

## Runtime Characteristics

- Scheduling integrates with process and signal handling flow.
- Queue mutation paths are guarded for safer concurrent behavior.
- Context-switch assembly path remains architecture-specific.

## Strengths

- Scheduler selection is now configuration-driven.
- API boundary allows policy iteration without broad call-site churn.
- Stability improvements reduced common queue corruption risks.

## Weak Points

- Alternative policies are intentionally minimal and still evolving.
- Deep fairness and latency tuning remains future work.
- SMP-oriented scheduling is not the current baseline target.

