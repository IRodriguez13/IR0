# KTM hardware mocks

Mocks implement the same `includes/ir0/*` facade used by production backends.
They exist only to reproduce success, failure, timeout, and recovery paths in
KTM scenarios.

## Layout

Place each implementation under `ktm/mocks/<hardware-family>/`. Test-control
headers use the `ktm/include/ktm_mock_*.h` namespace; they are not public
kernel APIs.

## Link contract

- A mock must be selected explicitly by a test-only `CONFIG_KTM_*` option.
- Product defconfigs must leave every mock disabled.
- Portable and production driver code must never include a mock header.
- Do not add `testing` branches, synthetic responses, or smoke markers to a
  production hardware backend. The harness observes the facade from outside.
- A mock may depend on a public facade, but never on a production driver's
  private header or state.

`make arch-guard` enforces the location, include boundary, and product-config
rules.
