# IR0 Kernel

IR0 is a research operating-system kernel (GPL-3.0). Primary bring-up target is
**x86-64** under QEMU (Multiboot, GRUB, VFS/MINIX, ELF userspace). Version
string: **`0.0.1-pre-rc3`**.

It is not a general-purpose production OS. The tree emphasizes narrow facades
(`includes/ir0/`), Kconfig selection, and honest partial Linux ABI (`-ENOSYS`
where unimplemented).

## Getting started

Complexity belongs in the code — starting should not.

Product userspace (runit + BusyBox) lives in a **sibling repo**. First boot:

```bash
git clone https://github.com/IRodriguez13/IR0.git
cd IR0
make check-env            # host diagnostic
make defconfig
make first-boot           # clone ../IR0-userspace if needed + minimal rootfs + ISO
make run                  # QEMU GTK → getty → BusyBox ash
```

Kernel-only ISO (no shell): `make ir0`. Without `/sbin/init` on `disk.img` the
kernel **panics** at handoff — there is no in-kernel fallback shell.

```bash
make sync-mandocs         # man pages → ~/.local/share/man (no sudo)
make man TOPIC=onboarding
make man TOPIC=boot
```

Full coupling guide: **[Documentation/USERSPACE.md](Documentation/USERSPACE.md)**.

| Profile | Meaning | Status |
|---------|---------|--------|
| `desktop` / `make desktop-x86_64` | x86-64 ISO + QEMU | **Supported** — official first boot |
| `userspace` | musl / BusyBox ISO path | **Experimental** |
| `hub-rpi4` | ARM64 RPi4 UART min | **Hardware lab / UART** — boots under QEMU `raspi4b` when present; **not** SD-flashable |
| `watch-rpi5-stub` | ARM64 RPi5 stub | **Planned** — compile-only (`uart=none`) |

```bash
make check-env PROFILE=desktop-x86_64
make run-bootlog                   # optional: boot log → build/hostshare/ir0-boot.log
make help-profiles
make pre-submit                    # local contributor gate → PRE_SUBMIT_OK
```

Full toolchain notes: **[SETUP.md](SETUP.md)**.
How to contribute: **[CONTRIBUTING.md](CONTRIBUTING.md)**.
Subsystem docs: **[Documentation/](Documentation/README.md)** — `make man TOPIC=…`.

## Context

**x86-64 (default):** uniprocessor kernel with RR scheduling, fork/exec/wait,
demand paging and fork COW paths exercised in-tree, VFS (MINIX root, tmpfs,
path-routed `/proc`/`/sys`), console/TTY, and a partial Linux syscall surface
for musl/BusyBox bring-up. Networking includes UDP/ICMP, **AF_UNIX** streams,
and lab-grade TCP (`sock_stream` / wire path with limited recovery) — not a
full Internet stack or production NIC story. Optional demos: BusyBox `ash` via
runit (`make load-userspace-runit` / `run-fase58e-ash-gui`), doomgeneric on `/dev/fb0`.

**ARM64:** early bring-up and board scaffolding (QEMU `virt`, RPi4 UART lab).
Not a flashable appliance and not feature-parity with x86.

**Non-goals today:** SMP, complete Linux syscall coverage, desktop-class
userspace, or “better than Linux.” Near-term focus is open, rebuildable
appliance profiles with a thin Makefile surface and actionable diagnostics.

## License

GNU General Public License v3.0 — see [LICENSE](LICENSE).
Third-party trees (BusyBox, doomgeneric, …) keep their own licenses under `setup/`.
