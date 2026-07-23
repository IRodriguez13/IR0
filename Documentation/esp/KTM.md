# KTM — Kernel Test Module

> **Última verificación:** 2026-07-23  
> **Fuente de verdad:** `includes/ir0/ktm/*`, `ktm/*.c`, `ktm/include/klog.h`,  
> `tests/ktm/`, `setup/Kconfig` (`CONFIG_KTM*`), `scripts/ktm_*.py`,  
> `scripts/smoke_autokill.py`, `scripts/make/class-b.mk`, targets `ktm-*` del Makefile  
> **Canónico (inglés):** [`../KTM.md`](../KTM.md)

KTM es el **plano canónico de test y diagnóstico** del kernel IR0: eventos tipados,
checkpoints, snapshots, escenarios in-kernel, `/dev/ktm` para casos desde userspace y
runners en el host que matan QEMU al ver tags. Los tags seriales legacy `[FASE` en
código kernel están **retirados** (`make arch-guard`).

Índice corto para agentes: [`../ai_driven_dev/ktm.md`](../ai_driven_dev/ktm.md).  
Mapas FASE: [`../KTM_FASE_PARITY.md`](../KTM_FASE_PARITY.md),
[`../KTM_FASE_INVENTORY.md`](../KTM_FASE_INVENTORY.md).

---

## 1. Arquitectura (dónde vive cada cosa)

```text
includes/ir0/ktm/     Facades públicas (kernel + uapi)
ktm/                  Internals de runtime (anillo, transport, registry, /dev/ktm, …)
tests/ktm/scenarios/  Escenarios in-kernel (enlazados al kernel)
tests/ktm/userdev/    Pilotos musl inyectados como /sbin/init en QEMU
tests/ktm/lib/        libktm_user — wrapper de ioctl
scripts/ktm_*.py      Runners / clasificadores en el host
```

Arranque en kernel:

1. `ktm_core_init()` — registry + registro de builtins.
2. `ktm_userdev_register()` — `/dev/ktm` si `CONFIG_KTM_USERDEV`.
3. `ktm_scenarios_run_boot()` — suite de escenarios (`kernel/main.c`).

---

## 2. Capas Kconfig

| Opción | Rol |
|--------|-----|
| `CONFIG_KTM` | Núcleo: checkpoints, panic class |
| `CONFIG_KTM_EVENTS` | Anillo tipado + transport `KTM|…` |
| `CONFIG_KTM_FLIGHT` | Dump del anillo en panic |
| `CONFIG_KTM_TEST` | Suite de escenarios al boot |
| `CONFIG_KTM_USERDEV` | Plano `/dev/ktm` |
| `CONFIG_KTM_FAULT` | Stub de inyección (off en prod) |
| `CONFIG_KTM_SERIAL_VERBOSE` | CHECKPOINT/PROBE ruidosos (default **n**) |

Si `KTM_USERDEV` está off, el header expone un `static inline` vacío de
`ktm_userdev_register()`; la definición real en `ktm/userdev.c` solo se compila con
la opción on.

### Capas de log (klog vs KTM vs host)

Detalle canónico: [`../KTM.md`](../KTM.md) (Logging layers).

| Capa | Rol |
|------|-----|
| **klog** | Eventos humanos `[ts] [LEVEL] [COMP]` — `<ir0/ktm/klog.h>`; banner BOOT primero (`klog_boot_hold`) |
| **KTM** | Transporte `KTM\|…`; `ASSERT_BATCH` colapsa loops felices; Class B gates |
| **runit tags** | `ir0_smoke_tag()` — mismo rol que `klog_smoke` para autokill |
| **QEMU host** | `*.qemu-stderr` separado del log serial del guest |

---

## 3. Internals (resumen)

| Pieza | Qué hace |
|-------|----------|
| Event ring | `ktm_event_emit4` / `read` vía `/dev/ktm` |
| Transport | Líneas `KTM|<seq>|<KIND>|<name>|<status>` |
| Snapshot | Conteos para asserts de leak |
| Assert | Soft asserts + **`ASSERT_BATCH`** |
| klog hub | `ktm/klog.c` — log humano |
| Scenario runner | Register / run / boot suite |
| Userdev | ioctl + poll del anillo |

---

## 4. API en kernel mode

```c
#include <ir0/ktm/ktm.h>

KTM_CHECKPOINT(KTM_CP_PROCESS_FORK);
ktm_event_emit4(KTM_EVENT_PROCESS_CREATE, KTM_SUBSYS_PROC, pid, 0, 0, 0);
```

Escenario nuevo: `tests/ktm/scenarios/*.c` + registro en
`ktm_scenarios_register_builtins()` + objeto en el Makefile.

Macros: `KTM_V1_ASSERT_TRUE`, `KTM_V1_ASSERT_EQ`, `KTM_REQUIRE` (aborta el `run`),
`KTM_ASSERT_NO_PROCESS_LEAK` / `KTM_ASSERT_NO_FRAME_LEAK`.

```bash
make -s ktm-run
make -s ktm-run SCENARIO=ipc.pipe_lifecycle
```

---

## 5. API en user mode (`/dev/ktm`)

```c
#include "libktm_user.h"

fd = ktm_open();
ktm_get_caps(fd, &caps);          /* hace falta KTM_CAP_USERDEV */
ktm_case_begin(fd, "mi_caso");
ktm_assert_true(fd, "ok", cond);
ktm_run_scenario(fd, "process.lifecycle", &rc);
ktm_case_end(fd, "mi_caso", fails ? 1 : 0);
```

Ioctls: `KTM_IOC_RUN_SCENARIO`, `RUN_INVARIANTS`, `TAKE_SNAPSHOT`, `RESET`,
`GET_CAPS`, `USER_EVENT` (ver `uapi.h`).

Piloto nuevo: `tests/ktm/userdev/ktm_*_case.c` + target `build-ktm-*-case` /
`ktm-userdev-*-run`. Runner: `scripts/ktm_userdev_runner.py` — por defecto stub
`init_hostshare_exec` en disco + payload `ir0_payload` en virtio-9p
(`make smoke-hostshare-exec`); `--legacy-disk-init` para el path antiguo.

**Híbrido producto:** wrapper `scripts/ktm_userdev_runit_run.sh` (T3 checklist /
`smoke-t3-prep`); PID1 **runit** + `runit_hostshare_payload_run` + `--disk`.
Stub `ktm-userdev-*-run` = lab. Detalle: [`../KTM.md`](../KTM.md) §5.4.

```bash
make -s smoke-t3-prep
make -s smoke-hostshare-exec
make -s ktm-userdev-run
make -s smoke-tcp-wire
make -s smoke-runit-boot
```

---

## 6. Política

1. Preferir KTM a tags seriales nuevos.
2. Tests en `tests/ktm/`; `ktm/` solo runtime.
3. Paridad FASE: consultar `KTM_FASE_PARITY.md` antes de marcar COVERED.
4. Mantener sync `event.h` ↔ `KTM_UAPI_EVENT_*` en `uapi.h`.

Detalle completo y tablas: **[`../KTM.md`](../KTM.md)**.
