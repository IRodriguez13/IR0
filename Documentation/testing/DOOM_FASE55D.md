# Doom T2 — doomgeneric real IWAD

> **Last verified:** 2026-09-22
> **Source of truth:** `setup/doom/doomgeneric_ir0.c`, `setup/make/legacy-smokes.mk`
> (`smoke-doomgeneric`; alias `smoke-fase55d-doomgeneric`),
> [`HARNESS_MAP.md`](../HARNESS_MAP.md).

Historical filename kept so existing links work.

## Purpose

Prove Doom-class fullscreen userspace on IR0: framebuffer, evdev input (keyboard
**and** mouse), and PCM audio — not only “opens and paints frames”.

## Build

```bash
make -s build-ktm-doomgeneric-smoke
# Flags: -DIR0_DOOM_PORT -DFEATURE_SOUND
```

Interactive variant: `make build-doom-interactive` (alias
`build-fase55e-doom-interactive`).

## Smoke

```bash
IR0_LEGACY_SMOKE=1 make smoke-doomgeneric \
  REAL_WAD_PATH=/path/to/doom1.wad
```

Defaults: `REAL_WAD_PATH` often set in `scripts/make/qa.mk`. Hybrid KTM alias:
`ktm-userdev-doom-55d-run`.

### Harness layout

| Piece | Path |
|-------|------|
| Doom ELF | `setup/doom/doomgeneric_smoke` → guest `/bin/doom-smoke` |
| runit init/run | `ISD/out/smoke/stage-bin/runit_fase55d_{init,run}` (`RUNIT_SMOKE_STAGE_BIN`; ISD name kept) |
| IWAD | injected as `/usr/share/doom/doom1.wad` |
| QEMU audio (GUI / `make run`) | `$(QEMU_AUDIO_SB16)` → default `-audiodev pa` |
| QEMU audio (smoke CI) | `$(QEMU_AUDIO_SB16_SILENT)` → `-audiodev none` |

`make build-runit` also builds the smoke service tree (`build-services.sh smoke`).

### Required serial tags

| Tag | Meaning |
|-----|---------|
| `DOOMGENERIC_MOUSE_CAPS_OK` | `/dev/events0` reports mouse / motion caps |
| `DOOMGENERIC_AUDIO_OK` | opened `/dev/audio` + format 11025/8/1 |
| `DOOMGENERIC_AUDIO_WRITE_OK` | PCM `write()` succeeded |
| `DOOMGENERIC_FIRST_FRAME_OK` | first blit |
| `DOOMGENERIC_FRAME_LOOP_OK` | frame budget finished |
| `KTM_DOOMGENERIC_OK` | smoke success |
| `KTM_DOOMGENERIC_CASE_OK` / `KTM_USERDEV_OK` | KTM product case |

Optional: `DOOMGENERIC_MOUSE_EVENT_OK` when the guest actually receives `EV_REL`
/ button events (not required for headless CI).

## Backend contracts

- **Mouse:** `EV_REL` (`REL_X`/`REL_Y`) and `BTN_LEFT`/`RIGHT`/`MIDDLE` → `D_PostEvent(ev_mouse)`.
- **Sound:** no `-nosound`; music stays `-nomusic`. Kernel path: `/dev/audio` → SB16 (`CONFIG_ENABLE_SOUND`).
- **Input:** `/dev/events0` non-blocking read each frame tick.

## Related

- [`../STABLE.md`](../STABLE.md) — T2 merge blocker
- [`../USERSPACE.md`](../USERSPACE.md) — coupling summary
- [`../DRIVERS.md`](../DRIVERS.md) — QEMU audio attach
- [`../HARNESS_MAP.md`](../HARNESS_MAP.md) — tag/target names
