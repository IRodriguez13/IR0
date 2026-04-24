# IR0 Filesystem Architecture

IR0 uses a VFS-first design where policy is centralized and backend filesystems
provide concrete operations.

## Active Layers

1. `fs/vfs.c`: path resolution, mount dispatch, open/read/write/chmod/chown flow.
2. Backend filesystems:
   - persistent: `minix`
   - in-memory: `tmpfs`
   - pseudo: `procfs`, `devfs`, `sysfs`
3. Storage/network/device drivers accessed through backend APIs, not direct calls.

## Current Filesystem Set

- Root filesystem: selected by config through `vfs_init_root()`.
- `procfs`: process and kernel runtime information.
- `devfs`: character/block style device entry points.
- `sysfs`: structured kernel and subsystem runtime state.
- `tmpfs`: volatile files and directories with uid/gid and umask behavior.
- `minix`: disk-backed baseline filesystem.

## Permission Model in Path

- Access checks are done against effective credentials (`euid`, `egid`).
- `chmod` policy: owner-or-root at syscall and VFS boundary.
- `chown` policy: root-only at syscall and VFS boundary.
- Backends (`tmpfs`, `minix`) also enforce policy to avoid bypasses.

## Semantics and Behavior

- Negative errno is returned consistently for failure paths.
- `O_TRUNC` is supported through VFS truncate operation dispatch.
- Relative paths are resolved against per-process `cwd`.
- `/proc` per-process contexts avoid pseudo-fd collisions across processes.

## Strengths

- Clear separation between VFS policy and backend implementation.
- Configurable root/backend composition via Kconfig and Makefile wiring.
- Good observability through pseudo-filesystem endpoints.

## Weak Points

- Backend parity is still evolving for advanced Unix semantics.
- Some metadata and edge-case behavior remains hobby-kernel grade.
- Heavy runtime correctness depends on broad integration testing.

