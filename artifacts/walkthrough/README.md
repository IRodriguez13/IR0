# ARM64 walkthrough evidence

These captures document the Raspberry Pi 3 QEMU bring-up reaching BusyBox EL0
and returning through the Linux syscall ABI. They were captured before the
2026-10-29 Makefile consolidation, so the command visible in the terminal is a
historical target. The current equivalent build entrypoint is:

```sh
make ir0 arm64 rpi3
```

The serial markers remain the relevant evidence: `ARM64_BUSYBOX_EL0_OK`,
`ARM64_BUSYBOX_EXIT_OK`, and `ARM64_SYSCALL_OK`.
