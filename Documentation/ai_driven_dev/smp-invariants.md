# SMP-3 lock inventory

> **Last verified:** 2026-11-21
> **Source of truth:** `kernel/process/registry.c`, `includes/ir0/process.h`, `sched/core.h`, `includes/ir0/spinlock.h`, and the objects below
> **Tree:** `dev` at `a72f19a8`

Record of the locks that exist in this tree. `ir0_spin_lock_irqsave` masks IRQs and does not touch the lock object (`includes/ir0/spinlock.h`; a `CONFIG_SMP` build hits `#error`). This list is the inventory SMP-3 will turn into an order.

## current_process

The variable is `process_t *current_process` in `kernel/process/registry.c`. That file owns the architecture-neutral process registry state. The declaration is `extern process_t *current_process` in `includes/ir0/process.h`.

`process_set_current()` in `registry.c` is the only writer. It also stores `&process->task` through `cpu_local_set_current_task()` (`includes/ir0/percpu.h`, field `current_task` of `struct ir0_cpu_local`). The `current_process` symbol is one global pointer.

`kernel/time/clock_wait.c` and `arch/arm64/sources/exc_early.c` repeat the `extern`. `tests/host/host_pmm_stubs.c` defines its own `current_process` for host tests.

## Runqueue lock

`rq_lock()` and `rq_unlock()` live in `sched/core.h`. Today they are `irq_save()` and `irq_restore()`. No `ir0_spinlock_t` backs them.

`CONFIG_SCHEDULER` links one policy file (`sched/core.h`, `Makefile`). The policy files call `rq_lock()`:

- `sched/rr.c` (`rr`): `sched_add`, `sched_remove`, `sched_pick_next`, `sched_count_runnable`, `sched_promote`. `sched_selftest` does not.
- `sched/prio.c` (`prio`): the same five, plus `sched_selftest`.

`sched/core.c` is not a policy file. `sched_switch_to()` calls `rq_lock()`. `schedule()` calls `sched_pick_next()` (which takes and drops the lock) and then `sched_switch_to()`.

## ir0_spinlock_t objects

23 objects. `tcp_lock` is defined in `net/tcp.c` and declared in `net/tcp_internal.h`; that is one object. Every object below is taken with `ir0_spin_lock_irqsave` and dropped with `ir0_spin_unlock_irqrestore`. There is no other acquire.

### fs

- `pipe_lock` (`fs/pipe.c`) — pipe ids, end counts, and ring state.
- `files_struct_lock` (`fs/files_lifecycle.c`) — `files_struct` refcount (`files_get`, `files_put`).
- `named_fifo_lock` (`fs/named_fifo.c`) — in-memory FIFO path table from `mknod`/`mkfifo`.
- `named_devnode_lock` (`fs/named_devnode.c`) — in-memory character and block device nodes from `mknod`.

### ipc

- `ipc_lock` (`ipc/ipc.c`) — channel table and message queues.

### mm

- `mm_struct_lock` (`mm/mm_struct.c`) — `mm_struct` refcount (`mm_get`, `mm_put`, `mm_users`).
- `reclaimer_lock` (`mm/reclaim/reclaim.c`) — reclaimer table. `reclaim_active` is a separate `unsigned long` taken with `test_and_set_bit_lock`, not this type.
- `swap_area_lock` (`mm/swap/swap.c`) — swap-area table (bitmaps, reference counts, in-use counts).
- `heap_lock` (`mm/alloc/allocator.c`) — kernel heap free list and statistics. `kmalloc`, `kcalloc`, and `kfree` reach it through `heap_alloc` / `heap_free`.
- `pmm_lock` (`mm/alloc/pmm.c`) — frame bitmap, reference counts, and statistics.

### drivers

- `tty_lock` (`drivers/tty/console.c`) — tty read-waiter table in `tty_sleep_for_input`.
- `input_events_lock` (`drivers/input/input_events.c`) — evdev client rings and the reader count.
- `rtl8139_lock` (`drivers/net/rtl8139.c`) — `rtl8139_rx_processing` in `rtl8139_try_enter_rx` and `rtl8139_leave_rx`.

### net

- `dhcp_lock` (`net/dhcp.c`) — `dhcp_state` (xid, offer, ack, nak) and the handler-registered flag.
- `sock_udp_lock` (`net/sock_udp.c`) — bound UDP sockets and per-socket RX queues.
- `arp_lock` (`net/arp.c`) — ARP cache and per-interface IP list.
- `tcp_lock` (`net/tcp.c`, extern in `net/tcp_internal.h`) — `tcp_wire_*` connection state. Also taken from `net/tcp_cc.c`, `net/tcp_input.c`, and `net/tcp_listen.c`.
- `udp_lock` (`net/udp.c`) — UDP port-handler list.
- `dns_lock` (`net/dns.c`) — pending DNS queries, the handler-registered flag, and the default server.
- `sock_icmp_lock` (`net/sock_icmp.c`) — open ICMP sockets and per-socket RX queues.
- `icmp_lock` (`net/icmp.c`) — pending ICMP echo requests and replies.

### kernel/process

- `sighand_lock` (`kernel/process/domains.c`) — `sighand_struct` refcount (`sighand_get`, `sighand_put`, `sighand_users`).
- `fork_link_lock` (`kernel/process/fork.c`) — vfork parent/child table and `fork_pending_child`.

`process_registry_lock()` in `kernel/process/registry_lock.c` masks IRQs. It is not an `ir0_spinlock_t`.

## Order where one function takes two locks

No function calls `ir0_spin_lock_irqsave` on two different objects in its own body. These are the nests written as a call while a lock is still held.

Two different `ir0_spinlock_t` objects:

- `named_fifo_create_owned` holds `named_fifo_lock` and calls `pipe_create`, which takes `pipe_lock`.
- `dns_resolve` holds `dns_lock` and calls `udp_register_handler`, which takes `udp_lock`.

The same object taken again:

- `ipc_purge_waiters_for_process` holds `ipc_lock` and calls `wait_queue_remove_process`, which takes `ipc_lock` again.
- `sock_icmp_release` holds `sock_icmp_lock` and calls `sock_icmp_list_remove`, which takes `sock_icmp_lock` again.

`pmm_owner_audit` holds `pmm_lock` and calls `process_find_by_pid`, which takes `process_registry_lock()`.

`heap_lock` is taken, via `kmalloc` / `kcalloc` / `kfree`, while another lock is still held:

- `named_fifo_create_owned` (`named_fifo_lock`): `pipe_abort_unopened` (`kfree`) and `pipe_create` (`kmalloc`).
- `ir0_swap_area_install` (`swap_area_lock`): `kmalloc` and `kcalloc`.
- `sched_remove` in `sched/rr.c` and `sched/prio.c` (`rq_lock`): `kfree`.
- `sock_udp_rx_enqueue` (`sock_udp_lock`): `kfree`.
- `arp_lookup_mac` (`arp_lock`): `kfree`. `arp_cache_add` (`arp_lock`): `kfree` in `arp_cache_remove_oldest`, then `kmalloc`. `arp_set_interface_ip` (`arp_lock`): `kmalloc`.
- `udp_register_handler` (`udp_lock`): `kmalloc`. `udp_unregister_handler` (`udp_lock`): `kfree`. `dns_resolve` is still holding `dns_lock` across the `udp_register_handler` call.
