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
| mm stats | `mm/mm_port.c` | `KTM_MM_PORT_MOCK` |
| platform ops | none (selected at runtime) | `KTM_PLATFORM_MOCK` |

The first five replace the production object at link time. `KTM_PLATFORM_MOCK`
instead links `ktm/mocks/platform/ktm_platform_mock.c`; tests select
`ktm_platform_mock_ops` explicitly through `ir0_platform_ops_set()`.

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
