# KTM hardware mocks

Mocks implement the same `includes/ir0/*` facade used by production backends.
They exist only to reproduce success, failure, timeout, and recovery paths in
KTM scenarios.

## Layout

Place each implementation under `ktm/mocks/<hardware-family>/`. Test-control
headers use the `ktm/include/ktm_mock_*.h` namespace; they are not public
kernel APIs.

| Family | Production object replaced | CONFIG option |
| --- | --- | --- |
| console | `kernel/console_backend.c` | `KTM_CONSOLE_MOCK` |
| input | `drivers/input/input_backend.c` | `KTM_INPUT_MOCK` |
| video | `drivers/video/video_backend.c` | `KTM_VIDEO_MOCK` |
| audio | `sound/audio_backend.c` | `KTM_AUDIO_MOCK` |
| mm stats | `mm/alloc/mm_port.c` | `KTM_MM_PORT_MOCK` |
| platform ops | none (selected at runtime) | `KTM_PLATFORM_MOCK` |

The first five replace the production object at link time. `KTM_PLATFORM_MOCK`
instead links `ktm/mocks/platform/ktm_platform_mock.c`; tests select
`ktm_platform_mock_ops` explicitly through `ir0_platform_ops_set()`.

## Registry-based fakes

Two hardware families are faked through their public registry instead of
link-time replacement:

| Family | Production seam | Fake | CONFIG option |
| --- | --- | --- | --- |
| block | `ir0_block_register()` (`ir0/blockdev.h`) | `ktm/mocks/block/ktm_block_fake.c` | `KTM_BLOCK_FAKE` |
| net | `net_register_device()` (`ir0/net.h`) | test-local `struct net_device` | none (register in the test) |

The block fake registers a bounded RAM disk and injects `-EIO` on a selected
read. Net tests register a `net_device` with their own `send`/`poll`/`get_stats`
callbacks, so no NIC driver or `CONFIG_DRV_NIC_*` is required. See
`tests/host/test_netdev_contract.c` and `ktest_proc_netinfo_contract`.

## Scope

Link-time mocks cover facades whose production object owns a specific device:
console, input, video, audio, MM stats, and platform power. Arch services
(serial, timer, interrupt controller, RTC hardware access) are not replaced:
their portable logic is host-tested, and the device access itself is the arch
boundary. RTC calendar logic is covered by `tests/host/test_rtc_calendar.c`;
tests can satisfy `rtc_read_time()` with a fake provider when they need a
deterministic wall clock.

## Link contract

- A mock must be selected explicitly by a test-only `CONFIG_KTM_*` option.
- Product defconfigs must leave every mock disabled.
- A `CONFIG_KTM_*_MOCK` option requires `CONFIG_KTM=y`.
- Portable and production driver code must never include a mock header.
- Do not add `testing` branches, synthetic responses, or smoke markers to a
  production hardware backend. The harness observes the facade from outside.
- A mock may depend on a public facade, but never on a production driver's
  private header or state.

`make arch-guard` enforces the location, include boundary, product-config and
KTM-dependency rules. `tests/host/test_backend_mock_contract.c` links every
mock and checks both success and failure branches through the public facade;
`make build-matrix-min` also builds a kernel with every mock enabled.
