# Seguridad y credenciales de IR0

| Campo | Valor |
|-------|-------|
| Versión | 0.2 |
| Fase IR0 | T0–T1 |
| Estado | stable |
| Depende de | process, vfs, syscalls |
| Página man | IR0-security (sección 7) |
| Fuentes principales | `kernel/credentials.c`, `kernel/elf_loader.c`, `kernel/process.h`, `fs/permissions.c`, `fs/vfs.c`, `includes/ir0/credentials.h` |

> **Última verificación:** 2026-07-25

## 1. Visión general

IR0 usa un modelo Unix de credenciales: `uid`/`gid` real, efectivo y guardado
por proceso, grupos suplementarios, `umask` y comprobaciones de ruta vía IDs
efectivos. No hay conjunto de capabilities Linux.

La elevación de privilegios es **política de userspace**, no un servicio del
kernel: el kernel aplica los bits set-user-ID/set-group-ID en `execve` y el
userspace de producto (`IR0-userspace`) trae **OpenDoas** con
`permit persist :wheel as root`. La syscall específica `sudo_auth` (404) y sus
contraseñas hardcodeadas se eliminaron; el número queda retirado en
`includes/uapi/ir0/syscall_linux.h`.

## 2. Arquitectura interna

| Pieza | Rol |
|-------|-----|
| `credentials.c` | `ir0_current_cred`, `ir0_check_file_access` |
| `permissions.c` | `ir0_access_from_stat` |
| `process_t` | uid, gid, euid, egid, `suid`, `sgid`, `groups[]`, `ngroups`, `no_new_privs`, umask |
| `elf_loader.c` | bits set-id en exec, `AT_SECURE` en auxv |
| VFS | comprobaciones traverse, política chmod/chown/mount |
| Syscalls | get/set{res}uid/gid, setgroups, umask, chmod, chown, access, `PR_SET_NO_NEW_PRIVS` |

**Base de cuentas:** `/etc/passwd`, `/etc/shadow` (modo 0600) y `/etc/group`
reales, generados por el árbol de userspace. Los hashes son SHA-512 `crypt(3)`;
no hay contraseñas en el código del kernel.

## 3. Flujo de datos

**Comprobación acceso a fichero:**

```text
  open/stat/access path
       → ir0_stat_path_routed (proc/sys/dev/vfs)
       → ir0_access_from_stat(st, mode, euid, egid)
       → euid==0 permite todo
       → else bits rwx owner/group/other en st_mode
```

**Elevación (exec set-id):**

```text
  execve(/usr/bin/doas)          ← modo 4755, dueño root
       → elf_loader lee st_mode
       → ¿no_new_privs?  → ignora los bits set-id
       → S_ISUID → euid = st_uid (suid conserva el id real para setresuid)
       → AT_SECURE = 1 en auxv
       → doas autentica al llamador contra /etc/shadow y verifica :wheel
```

Mapa ASCII:

```text
  syscall ──► ir0_current_cred() ──► euid/egid
                      │
                      ▼
              ir0_check_file_access
                      │
                      ▼
              backend VFS (minix/tmpfs también aplican)
```

## 4. Responsabilidades

- VFS: `check_dir_traverse` exige execute en cada componente de path (no root).
- chmod: root o dueño del fichero en syscall + frontera VFS.
- chown: solo root en VFS; backend minix también solo root.
- mount/umount: credencial root requerida.

## 5. Límites del subsistema

- La autenticación y la política de elevación viven en userspace
  (`IR0-userspace`), no en el kernel: el kernel solo aporta syscalls de
  identidad, exec set-id, `/proc/[pid]/stat` y comprobación de permisos.
- Sin PAM ni capabilities.

## 6. Relación con otros subsistemas

| Vecino | Interacción |
|--------|-------------|
| VFS | todas las operaciones de path |
| Process | herencia cred en spawn/fork |
| Syscalls | syscalls identidad y permisos |
| IR0-userspace | login/getty, `passwd`, OpenDoas; `smoke-setuid-exec`, `smoke-passwd`, `smoke-doas` |

## 7. Mapas visuales

```text
  sujeto (euid,egid) ──► objeto (st_uid,st_gid,mode)
              │
              ├─ root → permitir
              └─ else → coincidencia triple rwx
```

## 8. Invariantes importantes

1. `ROOT_UID`/`ROOT_GID` = 0; spawn predeterminado sin padre usa root + umask 0022.
2. `ir0_current_cred()` sin proceso devuelve stub root de arranque.
3. Sin comprobaciones `CAP_*` en ningún sitio.
4. `no_new_privs` se hereda en fork y desactiva los bits set-id en exec.
5. Los ficheros nuevos toman el uid/gid **efectivo** del creador (POSIX), que es
   lo que hace que el fichero de timestamp de persist de doas pase su
   comprobación de propiedad.
6. `vfs_utimens` despacha al backend; MINIX guarda `i_time` en segundos.
7. Las entradas de directorio MINIX v1 limitan los nombres a 14 bytes: centinelas
   como `/etc/ir0-noroot` deben respetar ese límite o se truncan en silencio.
8. ACLs no implementadas.

## 9. Consejos de depuración

- Comandos debug `whoami`, `id` muestran ids efectivos.
- `-EACCES` en traverse: falta execute en componente directorio.
- `-EPERM` en mount/chown: necesita euid root.

## 10. Roadmap futuro

- Conjunto capabilities.
- ACLs en tmpfs/minix (notado en IR0-vfs §10).
- Permisos de envío de señales.
- Namespaces — no planificado.

Ver: `IR0-vfs`, `IR0-process`, `IR0-syscalls`.
