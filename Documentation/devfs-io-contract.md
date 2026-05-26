# Devfs I/O Contract

This document defines the I/O boundary contract for `/dev` backends in IR0.

## Core rule

- `fs/devfs.c` device backends operate on **kernel buffers**.
- `sys_read` and `sys_write` in syscall layer own the user/kernel copy boundary.

## Backend API semantics

- `devfs_ops.read(entry, kbuf, count, offset)`:
  - reads into `kbuf` (kernel memory),
  - returns produced byte count or negative errno.
- `devfs_ops.write(entry, kbuf, count, offset)`:
  - consumes bytes from `kbuf` (kernel memory),
  - returns consumed byte count or negative errno.

## Prohibited in normal read/write backend path

- `copy_to_user(...)`
- `copy_from_user(...)`

for standard `/dev/*` read/write handlers.

## Explicit exception policy

Usercopy may exist in explicitly scoped helper paths (for example ioctl shims)
only if:

- the function is clearly marked and reviewed as boundary helper, and
- it is covered by architecture guard whitelist.

## Naming rule for explicit boundary helpers

Functions that intentionally touch userspace pointers must use explicit names:

- `*_to_user`
- `*_from_user`

and must not be reused as generic backend read/write helpers.

## Why

This prevents split ownership bugs where both syscall layer and backend attempt
to perform usercopy, which can cause incorrect reads, stalls, or corruption.
