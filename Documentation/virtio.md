# Virtio in IR0 (QEMU)

> **Last verified:** 2026-09-02  
> **Build note (2026-10-29):** the `smoke-*` targets quoted below were removed
> with the legacy QA catalog. Build the kernel with `make ir0`, attach disks with
> `make run DISK=…`, and pass extra QEMU flags through `IR0_QEMU_EXTRA`.
> **Source of truth:** `drivers/net/virtio_net.c`, `fs/hostshare_9p.c`,  
> `scripts/run_qemu.sh`, [`HOSTSHARE_PRODUCT.md`](HOSTSHARE_PRODUCT.md)

IR0 talks to the host through **legacy virtio-pci** devices in QEMU
(`disable-modern=on`). Two product paths matter for day-to-day use:

| Device | Guest view | What it is for |
|--------|------------|----------------|
| **virtio-9p-pci** | `mount -t 9p ir0share /mnt/host` | Host directory ↔ guest files (drop ELF, edit, chaos R/W) |
| **virtio-net-pci** | `virt0` / stack behind `ifconfig` | L3 reachability / ping (alternate to RTL8139) |

**Not** in scope yet: virtiofs/FUSE, virtio-blk as primary root, VirtualBox
shares, modern virtio (1.0) rings.

Spanish summary: [`esp/virtio.md`](esp/virtio.md).

## virtio-9p (hostshare)

### QEMU flags (canonical)

```bash
SHARE=/tmp/ir0-share   # host directory
mkdir -p "$SHARE"
echo from_host > "$SHARE/host_marker.txt"

qemu-system-x86_64 \
  -cdrom kernel-x64.iso \
  -drive file=disk.img,format=raw,if=ide,index=0 \
  -fsdev local,id=ir0fs,path=$SHARE,security_model=none \
  -device virtio-9p-pci,fsdev=ir0fs,mount_tag=ir0share,disable-modern=on \
  -serial stdio -m 256M
```

`mount_tag=ir0share` must match the guest mount source name.

### Inside the guest

```sh
mkdir -p /mnt/host
mount -t 9p ir0share /mnt/host
ls /mnt/host
cat /mnt/host/host_marker.txt
echo guest_ok >/mnt/host/from_guest.txt
umount /mnt/host
# Host should now see $SHARE/from_guest.txt
```

BusyBox may need `busybox mount -t 9p …` if `mount` is not the applet you expect.

### Automated checks

The old `smoke-hostshare-*` / `smoke-session-chaos` targets no longer exist.
Drive the same sequence manually from a shell in the guest (the commands above)
or script it against a known ISD-built disk.

Product “drop a binary” recipe: [`HOSTSHARE_PRODUCT.md`](HOSTSHARE_PRODUCT.md).

### Manual interactive (GTK)

```bash
make -s ir0                        # rebuild kernel-x64.iso after kernel changes
# Ensure disk has accounts (ISD rootfs or seed). Then:
make run DISK=$IR0_ISD_DISK
```

To use the `ir0share` tag instead of the automatic `dennis` share:

```bash
IR0_DENNIS_9P=0 IR0_QEMU_EXTRA="\
  -fsdev local,id=ir0fs,path=$SHARE,security_model=none \
  -device virtio-9p-pci,fsdev=ir0fs,mount_tag=ir0share,disable-modern=on" \
  make run DISK=$IR0_ISD_DISK
```

Mount `/mnt/host` after login; edit on the host, re-read in the guest (or reverse).

## virtio-net

Legacy `virtio-net-pci` probes before RTL8139 when present. Interface name in
guest: typically **`virt0`**.

```bash
# Manual check: boot without rtl8139 (IR0_NET disables the default NIC).
IR0_NET=0 IR0_QEMU_EXTRA="-device virtio-net-pci,netdev=n0,disable-modern=on \
  -netdev user,id=n0" make run console
# Expect: F8_NIC_REACH_OK + NIC_PING_REPLY_OK
```

Manual QEMU sketch (user networking):

```bash
qemu-system-x86_64 ... \
  -netdev user,id=n0 \
  -device virtio-net-pci,netdev=n0,disable-modern=on
```

Then in guest: `ifconfig`, `route`, `ping -c 3 10.0.2.2` (user-net gateway).
Default product images often use RTL8139; virtio-net is the alternate NIC path.

## Session chaos (coherence)

The old `smoke-session-chaos` target is gone; run the same awkward but legal
commands from getty manually:

- `cat` / `head` on `/dev/null`, `/dev/zero`, `/dev/urandom`, write to `/dev/full`
- stderr redirect (`2>/home/...`)
- pipes (soft set — may `^C` on hang)
- `/proc/*`, `ps`, `mount`
- `doas` + tmpfs mount/umount under `$HOME`
- virtio-9p mount, read host marker, write guest file, umount (host must see file)

Hard fail: panic, `KERNEL_UACCESS_FAULT`, `STACK_TOP_OVERRUN`,
`CONSOLE_SESSION_SEGV`, missing prompt, missing 9p host file after umount.

**Notes from 2026-09-02 runs:** unprivileged `/tmp` may deny writes on packed
MINIX root — use `$HOME`. Mounts need `doas` (wheel password). HMP sendkey
automation can trip ash `STACK_TOP_OVERRUN` under load; prefer
`smoke-hostshare-9p` for a focused 9p gate, and manual GTK for exploratory chaos.

## Honesty / traps

| Topic | Note |
|-------|------|
| Stale ISO | Rebuild `kernel-x64.iso` (`make ir0`) and md5-match the extracted kernel |
| `disable-modern=on` | Required for current drivers |
| Serial vs VGA | Smoke tags on `/dev/serial` may look “stuck” to the prompt in combined logs |
| Pipe soft hangs | Soft set in chaos may `^C` and skip — same flake class as the old pipe-stress smoke |
| virtiofs | Not implemented; do not document as working |

## Related

- [`HOSTSHARE_PRODUCT.md`](HOSTSHARE_PRODUCT.md) — drop ELF → guest exec  
- [`USERSPACE.md`](USERSPACE.md) — ISD disk / firstboot  
- [`uaccess.md`](uaccess.md) — why `KERNEL_UACCESS_FAULT` is fatal in session soaks  
- [`BACKLOG_REMAINING.md`](BACKLOG_REMAINING.md) — F0/F8 virtio status rows  
