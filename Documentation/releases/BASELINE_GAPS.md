# Baseline problem inventory (lista §§1–18)

> **Última verificación:** 2026-07-27  
> **Fuente de verdad:** código citado; no hardcodes de éxito

| problema | ubicación | causa probable | gravedad | estado |
|----------|-----------|----------------|----------|--------|
| Dual task/process READY/RUNNING/BLOCKED | process.h / task.h | dos writers | P0 | **mitigado** — `process_set_sched_state` + `lifecycle` |
| Parent fork mutate before attach | fork.c | prepare_parent early | P0 | **mitigado** — prepare post-attach |
| Fault sites fork incompletos | fork.c | pocos KTM_FAULT_HIT | P0 | **mitigado** — kstack/cow/mmap/files/arch |
| task_get_rdi en API portable | arch_task.h | nombres x86 | P0 | **mitigado** — `task_get_argN` |
| MM mirrors divergencia | process.h / mm_struct | mirrors + mm | P1 | **cerrado** — mirrors eliminados; solo `process->mm` |
| arch_syscall_frame / TLS layout | process.h | frame x86 + fs_base | P1 | **mitigado** — `arch_syscall_frame_t` + `arch_thread` union |
| read(dir O_DIRECTORY) EBADF | fs_syscalls.c | vfs_file NULL | P1 | **mitigado** — `-EISDIR` |
| /proc/ps sin header | procfs.c | formato raw | P1 | **mitigado** — header PID/PPID/S/UID/CMD |
| Contract suites incompletas | tests/host | faltaban net/sched/block | P1 | **mitigado** — suites nombradas |
| os-release ID=ir0 | stage-rootfs.sh | branding | P2 | **mitigado** — ISD 0.1 en staging |
| Baseline QEMU capture | baseline_capture.py | checklist only | P1 | **PASS** — `--qemu` (uname…find /heart), login waits Password: |
| Net applets BusyBox off | busybox defconfig | ABI parcial | P2 | abierto — ver NETWORKING_MATRIX |
| PMM solo irq_save | mm/pmm.c | no SMP spinlock | P2 | abierto — no declarar SMP |
| ARM64 OS+userspace | BACKLOG | bring-up ≠ ISD | P2 | abierto — honest |

## Captura funcional

```bash
python3 scripts/baseline_capture.py
python3 scripts/baseline_capture.py --qemu   # needs iso + disk.img
```

Artefactos: `out/baseline/{PINS,PROBLEMS,CAPTURE}.md`, serial en `out/baseline/qemu-serial.log`.
