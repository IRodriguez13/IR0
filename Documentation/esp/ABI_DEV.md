# Comportamiento en `dev` (2026-11-20)

> **Última verificación:** 2026-11-20
> **Fuente de verdad:** `dev` en `93bccdf8`. El canónico en inglés es
> [`../ABI_DEV.md`](../ABI_DEV.md).

Al integrar en `dev` se corre la batería completa de gates sobre el árbol que
ya contiene el cambio.

| Comportamiento | Commit | Contrato |
|---|---|---|
| Tabla de fds ampliable | `9d3ceb58` | Crece con `files_grow()` hasta `RLIMIT_NOFILE`, tope 4096. Default blando 1024. Un asignador: `fd_alloc_lowest()`. Selftest `nofile`. |
| Commit anónimo lazy | `de8cafef` | `PROT_NONE` y `MAP_NORESERVE` (`0x4000`) anónimos no pueblan PTEs. Un `mmap` anónimo de lectura/escritura sin ese flag se puebla en el acto. Selftest `vm_commit`. |
| `ucontext` de Linux | `921a2121` | El handler edita el `ucontext` dentro del sigframe. `rt_sigreturn` lo importa. En arm64 hay un solo `mcontext`. Selftest `ucontext`. |
| Tipo de hipervisor | `bf4fbb05` | `hypervisor_kind()` es la sonda. `-1` significa que no corrió, no que sea bare metal. x86 usa CPUID `0x40000000`. arm64 usa el UID SMCCC para KVM y un nodo FDT `xen,xen` para Xen. `/proc/cpuinfo` imprime `hypervisor_kind`. `hypervisor_present()` es el predicado portable encima de esa sonda. Selftest `hypervisor_kind`. |
| Dirección del peer en `accept4` | `89ceacc8` | En `AF_UNIX`, `sys_accept4` escribe el nombre del peer igual que `getpeername`. `addr == NULL` no copia. Si la copia falla, cierra el fd nuevo. Selftest `accept4`. |
| `CLONE_SIGHAND` | `93bccdf8` | Comparte una `sighand_struct`. Sin `CLONE_VM` es `-EINVAL`. Con `CLONE_VM` y sin `CLONE_THREAD` comparte el `mm`. `SIG_IGN` actualiza a todos los que comparten la tabla. `exec` la copia si el refcount es mayor que uno. Selftest `sighand`. |

`hypervisor_vendor()` sigue siendo por ISA: x86 lee CPUID; arm64 no fabrica una cadena x86.
