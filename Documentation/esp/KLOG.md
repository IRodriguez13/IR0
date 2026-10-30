# IR0 — Núcleo de eventos klog

> **Última verificación:** 2026-07-24  
> **Fuente de verdad:** `includes/ir0/klog_event.h`, `ktm/klog.c`,  
> `fs/procfs.c`, `fs/devfs.c`, `kernel/main.c`

Versión en español de [`../KLOG.md`](../KLOG.md). El logging humano es un
**núcleo de eventos** (secuencia, fase, `clock_state`, `event_id`). KTM es
observador opcional; `CONFIG_KTM=n` no apaga klog ni `/proc/kmsg`.

Producto diario: `make run DISK=…` → runit/getty/ash (ISD). El dbgshell
in-kernel fue eliminado; ver [`USERSPACE.md`](USERSPACE.md).
Perfiles: `LOG_PROFILE_*` + cmdline `ir0.loglevel=` / `ir0.trace=` (ver inglés).
Exploración T0: `cat /proc/kmsg` desde ash.

Gates: `make ktest`, `make ctr`; hostshare es opt-in
(`CONFIG_BOOT_LOG_HOSTSHARE=y` + 9p `ir0share`).
