# KTM — Kernel Test Module

> **Last verified:** 2026-07-17  
> **Source of truth:** `includes/ir0/ktm/*`, `ktm/*.c`, `tests/ktm/`,  
> `setup/Kconfig` (`CONFIG_KTM*`), `scripts/ktm_*.py`, root `Makefile` targets `ktm-*`

KTM is IR0’s **canonical kernel test and diagnostic plane**: typed events, checkpoints,
snapshots, in-kernel scenarios, `/dev/ktm` for userspace-driven cases, and host runners
that autokill QEMU on protocol tags. Legacy kernel `[FASE` serial tags are **retired**
(enforced by `make arch-guard`).

Agent-oriented short index: [`ai_driven_dev/ktm.md`](ai_driven_dev/ktm.md).  
FASE migration maps: [`KTM_FASE_PARITY.md`](KTM_FASE_PARITY.md),
[`KTM_FASE_INVENTORY.md`](KTM_FASE_INVENTORY.md).

---

## 1. Architecture (what lives where)

```text
includes/ir0/ktm/     Public facades (kernel + uapi headers)
ktm/                  Runtime internals (ring, transport, registry, /dev/ktm, …)
tests/ktm/scenarios/  In-kernel scenario TUs (linked into the kernel)
tests/ktm/userdev/    musl pilots injected as /sbin/init for QEMU
tests/ktm/lib/        libktm_user — thin ioctl wrapper for pilots
scripts/ktm_*.py      Host runners / classifiers / inventory
```

| Layer | Path | Role |
|-------|------|------|
| Public kernel API | `#include <ir0/ktm/ktm.h>` (or `<ktm.h>` via thin `includes/ir0/ktm.h`) | Umbrella: events, asserts, scenarios, uapi, userdev hook |
| Private helpers | `#include <ktm_internal.h>` (`ktm/include/`) | Only inside `ktm/` and scenario TUs — **no** `../` or quoted includes (arch-guard `[ktm-include]`) |
| Userspace ABI | `#include <ir0/ktm/uapi.h>` | Ioctl numbers, caps, event/snapshot structs |
| Userspace helper | `tests/ktm/lib/libktm_user.h` | `ktm_open` / case / assert / scenario wrappers |

Makefile puts `-Iktm/include` **before** `-Iincludes/ir0` so `<ktm.h>` is not shadowed by the facade.

Boot wiring (kernel):

1. `ktm_core_init()` — registry + `ktm_scenarios_register_builtins()` (`ktm/registry.c`).
2. `ktm_userdev_register()` — `/dev/ktm` when `CONFIG_KTM_USERDEV`.
3. `ktm_scenarios_run_boot()` — runs registered scenarios (`kernel/main.c`).

---

## 2. Kconfig tiers

| Option | Default | Role |
|--------|---------|------|
| `CONFIG_KTM` | y | Core: checkpoints, panic class, snapshots hooks |
| `CONFIG_KTM_EVENTS` | y | Typed ring + serial `KTM|…` transport |
| `CONFIG_KTM_FLIGHT` | y | Dump ring on panic |
| `CONFIG_KTM_TEST` | (defconfig) | Boot scenario suite |
| `CONFIG_KTM_USERDEV` | depends on KTM+EVENTS | `/dev/ktm` control plane |
| `CONFIG_KTM_FAULT` | n | Fault-injection stub (v2); keep off in prod |
| `CONFIG_KTM_PROBE_DIAG` / `MALLOC_FORENSICS` | n | Temporary bring-up noise |

When `CONFIG_KTM_USERDEV` is off, `includes/ir0/ktm/userdev.h` provides an empty
`static inline ktm_userdev_register()`. The real definition in `ktm/userdev.c` is
compiled only when the option is on — keep that pairing intact.

---

## 3. Internals (runtime under `ktm/`)

| Component | Files | Behavior |
|-----------|-------|----------|
| Event ring | `event_ring.c`, `includes/ir0/ktm/event.h` | Typed `ktm_event_t`; `ktm_event_emit4`; `ktm_event_copy_out` for `read(2)` |
| Transport | `transport_serial.c` | Host-visible lines: `KTM|<seq>|<KIND>|<name>|<status>` |
| Registry / probes | `registry.c`, `probe.h` | Named probes (`ktm_probe_register` / `ktm_probe_run`) |
| Snapshot | `snapshot.c` | Process/frame/fd/pipe counts for leak checks |
| Assert | `assert.c` | `KTM_V1_ASSERT_*`, `KTM_REQUIRE`, leak helpers |
| Checkpoint | `checkpoint.c` | Lifecycle enum → event + transport |
| Scenario runner | `scenario.c` | Register / run one / run boot suite; pass/fail counters |
| Userdev | `userdev.c` | `/dev/ktm` ioctl + read/poll over the ring |
| Flight / panic | `ktm_flight.c`, `ktm_panic_class.c`, … | Post-mortem classification |
| Diags | `d1_*`, `ktm_probe_diag.c` | Optional forensics (Kconfig off by default) |

### Serial protocol (grep-friendly)

Examples emitted by scenarios / userdev:

```text
KTM|706|TEST_BEGIN|tcp_wire
KTM|707|CHECKPOINT|tcp_wire_connect
KTM|708|ASSERT_PASS|tcp_wire_send
KTM|709|TEST_END|tcp_wire|PASS
KTM|…|SUITE_END|…|…
```

User-driven cases also print plain tags (e.g. `KTM_USERDEV_OK`, `F8_TCP_WIRE_OK`) for
`scripts/smoke_autokill.py` / `ktm_userdev_runner.py` `--done` / `--require`.

---

## 4. Kernel-mode API — write and run tests

### 4.1 Include

```c
#include <ir0/ktm/ktm.h>   /* preferred from portable kernel code */
/* Scenario TUs may use: */
#include <ktm_internal.h>
```

### 4.2 Emit diagnostics from production paths

```c
KTM_CHECKPOINT(KTM_CP_PROCESS_FORK);

ktm_event_emit4(KTM_EVENT_PROCESS_CREATE, KTM_SUBSYS_PROC,
                (uint64_t)pid, 0, 0, 0);
```

`KTM_CHECKPOINT` is a no-op if `CONFIG_KTM` is off.

### 4.3 In-kernel scenario (boot suite)

1. Add `tests/ktm/scenarios/foo_bar.c`.
2. Implement `run(ktm_context_t *)` returning `KTM_OK` (0) or negative / `-1` on fail.
3. Register via `ktm_scenario_register` from a `ktm_scenario_register_foo_bar()` called
   inside `ktm_scenarios_register_builtins()` in `process_lifecycle.c` (current hub).
4. Add `tests/ktm/scenarios/foo_bar.o` to the kernel object list in the root `Makefile`.

Minimal pattern (see `tests/ktm/scenarios/process_lifecycle.c`):

```c
static int scenario_foo_run(ktm_context_t *ctx)
{
	ktm_system_snapshot_t before, after;

	(void)ctx;
	KTM_REQUIRE(ktm_snapshot_take(&before) == 0);
	KTM_REQUIRE(ktm_invariants_run(KTM_INV_PROCESS | KTM_INV_FRAMES) == 0);

	/* … exercise subsystem … */
	KTM_V1_ASSERT_TRUE(condition);
	KTM_CHECKPOINT(KTM_CP_PROCESS_EXIT);

	KTM_REQUIRE(ktm_snapshot_take(&after) == 0);
	KTM_ASSERT_NO_PROCESS_LEAK(&before, &after);
	KTM_ASSERT_NO_FRAME_LEAK(&before, &after);
	return KTM_OK;
}

static const ktm_scenario_t scenario_foo = {
	.name = "foo.bar",
	.flags = 0,
	.setup = NULL,
	.run = scenario_foo_run,
	.teardown = NULL,
};

void ktm_scenario_register_foo_bar(void)
{
	(void)ktm_scenario_register(&scenario_foo);
}
```

**Assert macros (kernel):**

| Macro | Behavior |
|-------|----------|
| `KTM_V1_ASSERT_TRUE(expr)` | Soft assert; records pass/fail on context |
| `KTM_V1_ASSERT_EQ(a, b)` | Soft equality |
| `KTM_REQUIRE(expr)` | Fail assert + `return -1` from `run` |
| `KTM_ASSERT_NO_PROCESS_LEAK(b, a)` | Snapshot delta |
| `KTM_ASSERT_NO_FRAME_LEAK(b, a)` | Snapshot delta |

**Invariants mask:** `KTM_INV_PROCESS`, `KTM_INV_FRAMES`, `KTM_INV_ALL`
(`includes/ir0/ktm/ktm.h`).

### 4.4 Run kernel scenarios from the host

```bash
make -s ktm-run                          # default SCENARIO=process.lifecycle
make -s ktm-run SCENARIO=ipc.pipe_lifecycle
```

Uses `scripts/ktm_runner.py` against `kernel-x64-userspace.iso` and expects
`TEST_END|<name>|PASS` on serial.

Boot suite runs **all** registered scenarios when `CONFIG_KTM_TEST` is enabled
(`ktm_scenarios_run_boot`).

---

## 5. Userspace API — drive tests via `/dev/ktm`

Requires `CONFIG_KTM_USERDEV`. Device node: `/dev/ktm` (devfs).

### 5.1 UAPI (`includes/ir0/ktm/uapi.h`)

| Ioctl | Purpose |
|-------|---------|
| `KTM_IOC_RUN_SCENARIO` | Run named in-kernel scenario; fills `result` |
| `KTM_IOC_RUN_INVARIANTS` | `ktm_invariants_run(mask)` |
| `KTM_IOC_TAKE_SNAPSHOT` | Frame/process/fd/pipe counts |
| `KTM_IOC_CONFIG_FAULT` | Fault inject (`-ENOTSUPP` if `KTM_FAULT` off) |
| `KTM_IOC_RESET` | Reset suite / ring cursor |
| `KTM_IOC_GET_CAPS` | `version` + `caps` bitmask |
| `KTM_IOC_USER_EVENT` | Emit CASE/ASSERT/CHECKPOINT from userspace |

Caps bits: `KTM_CAP_EVENTS`, `KTM_CAP_TEST`, `KTM_CAP_FAULT`, `KTM_CAP_USERDEV`.

`read(2)` on `/dev/ktm` returns zero or more `ktm_uapi_event_t` (same layout as
`ktm_event_t`). Poll uses `ktm_event_pending()`.

### 5.2 Helper library (`tests/ktm/lib`)

```c
#include "libktm_user.h"

int fd = ktm_open();                    /* open("/dev/ktm", O_RDWR) */
ktm_user_caps_t caps;
ktm_get_caps(fd, &caps);                /* require KTM_CAP_USERDEV */
ktm_reset(fd);
ktm_case_begin(fd, "my_case");
ktm_checkpoint(fd, "step1");
ktm_assert_true(fd, "cond_ok", cond);
ktm_assert_eq_u64(fd, "count", expect, actual);
ktm_run_scenario(fd, "process.lifecycle", &rc);
ktm_run_invariants(fd, KTM_INV_PROCESS | KTM_INV_FRAMES);
ktm_snapshot_request(fd, &snap);
ktm_case_end(fd, "my_case", fails == 0 ? 0 : 1);
ktm_close(fd);
```

### 5.3 Add a userspace pilot

1. Create `tests/ktm/userdev/ktm_my_case.c` (static musl PID1; often `pause()` or `_exit`
   after printing a done tag).
2. Wire `KTM_MY_CASE_SRC` / `BIN` and `build-ktm-my-case` + `ktm-userdev-my-*-run` in
   the root `Makefile` (copy an existing `build-ktm-*-case` block).
3. Compile with `-Iincludes -Itests/ktm/lib` and link `libktm_user.c`.
4. Optional: write a result file under virtio-9p `/mnt/host/…` and pass
   `--host-file` / `--host-grep` to `scripts/ktm_userdev_runner.py`.

Reference pilots: `ktm_fork_wait_case.c`, `ktm_tcp_wire_case.c`,
`ktm_posix_pseudofs_case.c`.

### 5.4 Host runner

```bash
make build-ktm-fork-wait-case
make build-init-hostshare-exec       # stub PID1: mount 9p + execve payload
make smoke-hostshare-exec            # stub on disk + fork_wait as ir0_payload
make ktm-userdev-run                 # default: stub init + share payload
make ktm-userdev-fork-storm-virtfs-run
make smoke-tcp-wire                  # F8-3 alias → tcp_wire virtfs run
```

`scripts/ktm_userdev_runner.py` (default **share-payload** path):

- Copies `disk.img`, injects **stub** `setup/pid1/init_hostshare_exec` as `/sbin/init`.
- Copies `--init` ELF to the virtio-9p share as **`ir0_payload`** (flat name).
- Always attaches `-fsdev` / `virtio-9p-pci` (`mount_tag=ir0share`).
- Stub mounts `/mnt/host` and `execve("/mnt/host/ir0_payload")`.
- Cases report via `ktm_hostshare_report()` (tolerates share already mounted).
- `--legacy-disk-init` restores old inject-`--init`-as-`/sbin/init` behavior.
- Extra QEMU args via `--qemu-arg`; if any arg contains `netdev`, no `-net none`.
- Autokills on `--done`; checks `--require` and optional `--host-file` / `--host-grep`.

---

## 6. Gates cheat sheet

```bash
# In-kernel suite / single scenario
make -s ktm-run
make -s ktm-run SCENARIO=mm.cow_fork

# Userspace /dev/ktm pilots
make -s ktm-userdev-run
make -s ktm-userdev-cow-run
make -s ktm-userdev-fork-storm-virtfs-run
make -s ktm-userdev-exec-drain-virtfs-run
make -s ktm-userdev-reap-drain-virtfs-run
make -s ktm-userdev-init-exit-drain-virtfs-run
make -s ktm-userdev-posix-pseudofs-virtfs-run
make -s ktm-userdev-input-det-virtfs-run
make -s smoke-nic-reach          # F8-1
make -s smoke-tcp-guest          # F8-2
make -s smoke-tcp-wire           # F8-3
make -s ktm-userdev-epoll-run    # epoll create/ctl/wait (canonical)
make -s ktm-userdev-stream-sock-run  # AF_UNIX + TCP loopback
make -s smoke-epoll-basic        # alias → ktm-userdev-epoll-run
make -s smoke-stream-sock        # alias → ktm-userdev-stream-sock-run

# Hygiene
make -s arch-guard               # no [FASE in kernel trees; ktm-include rules
make -s ktm-check                # host + classify selftest + panic inventory
python3 scripts/ktm_log_classify.py /tmp/some.log
```

Deep COW data-plane remains `make smoke-mm-cow-lazy` (not replaced by `mm.cow_fork`
bookkeeping scenario). Host-share: `make smoke-hostshare-9p` (MVP read) and
`make smoke-hostshare-exec` (stub + payload exec).

---

## 7. Policy

1. Prefer KTM scenarios / `libktm_user` / `KTM_CHECKPOINT` over new serial slogans.
2. Do not grow `ktm/` with test cases — put scenarios under `tests/ktm/scenarios/` and
   pilots under `tests/ktm/userdev/`.
3. Do not claim a FASE oleada “covered” without checking
   [`KTM_FASE_PARITY.md`](KTM_FASE_PARITY.md).
4. Keep uapi event type numbers in sync between `event.h` and `uapi.h`
   (`KTM_UAPI_EVENT_*`).

### 7.1 Canonical test plane (when KTM is better)

| Plane | Owns |
|-------|------|
| **KTM** (`ktm-run` / `ktm-userdev-*`) | Kernel state, fork/exec/IPC depth, guest net wire, fault injection, syscall depth that fits a musl pilot |
| **Keep outside KTM** | `arch-guard`, `tests/host` ABI/facade units, `linux-abi-audit-*`, deep COW `smoke-mm-cow-lazy`, product HOST (runit/ash/BusyBox/Doom), storage/hw/ARM machine smokes |
| **Do not add** | A new serial-only smoke for a gap KTM already covers better — add/extend scenario or userdev + `--require KTM_*` / `KTM\|…` protocol |

Overlapping product smokes must **alias** to the KTM gate as the primary target (e.g. `smoke-epoll-basic` → `ktm-userdev-epoll-run`, `smoke-stream-sock` → `ktm-userdev-stream-sock-run`). Legacy FASE serial storms remain SUB / non-default.

---

## 8. Related docs

| Doc | Content |
|-----|---------|
| [`tests/ktm/README.md`](../tests/ktm/README.md) | Tree layout for test artifacts |
| [`ai_driven_dev/ktm.md`](ai_driven_dev/ktm.md) | Agent-facing summary |
| [`KTM_FASE_PARITY.md`](KTM_FASE_PARITY.md) | FASE → KTM coverage class |
| [`KTM_FASE_INVENTORY.md`](KTM_FASE_INVENTORY.md) | Legacy smoke → KTM gate map |
| [`STABLE.md`](STABLE.md) | Release gates that mention KTM / net smokes |
