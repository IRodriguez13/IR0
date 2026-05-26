# Seguridad y credenciales de IR0

| Campo | Valor |
|-------|-------|
| Versión | 0.1 |
| Fase IR0 | T0–T1 |
| Estado | stable |
| Depende de | process, vfs, syscalls |
| Página man | IR0-security (sección 7) |
| Fuentes principales | `kernel/credentials.c`, `fs/permissions.c`, `fs/vfs.c`, `kernel/syscalls.c`, `includes/ir0/permissions.h` |

## 1. Visión general

IR0 usa un modelo mínimo estilo Unix de credenciales: `uid/gid/euid/egid` por
proceso, `umask` y comprobaciones de ruta vía IDs efectivos. No hay conjunto de
capabilities Linux. La elevación para flujos de depuración usa la syscall
específica IR0 **`sudo_auth`** con contraseñas hardcodeadas en `fs/permissions.c`.

## 2. Arquitectura interna

| Pieza | Rol |
|-------|-----|
| `credentials.c` | `ir0_current_cred`, `ir0_check_file_access` |
| `permissions.c` | `ir0_access_from_stat`, `auth_user_password` |
| `process_t` | uid, gid, euid, egid, umask |
| VFS | comprobaciones traverse, política chmod/chown/mount |
| Syscalls | get/set uid/gid, umask, chmod, chown, access, sudo_auth |

**Usuarios hardcodeados (`permissions.c`):**

```text
  root / uid 0 / password "root"
  user / uid 1000 / password "ir0"
```

## 3. Flujo de datos

**Comprobación acceso a fichero:**

```text
  open/stat/access path
       → ir0_stat_path_routed (proc/sys/dev/vfs)
       → ir0_access_from_stat(st, mode, euid, egid)
       → euid==0 permite todo
       → else bits rwx owner/group/other en st_mode
```

**sudo_auth:**

```text
  sys_sudo_auth(password)
       → si ya root: éxito
       → auth_user_password → al coincidir fija euid=0, egid=0 (permanente para el proceso)
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

- Sin grupos suplementarios; solo uid/gid efectivos.
- Sin aplicación de bit setuid/setgid **en exec**.
- Contraseñas en texto plano en fuente — no modelo de seguridad de producción.
- Sin `/etc/shadow`, PAM ni capabilities.

## 6. Relación con otros subsistemas

| Vecino | Interacción |
|--------|-------------|
| VFS | todas las operaciones de path |
| Process | herencia cred en spawn/fork |
| Syscalls | syscalls identidad y permisos |
| debug_bins | `cmd_sudo`, `cmd_whoami` vía syscalls |

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
4. `vfs_utimens` stub — no aplica timestamps reales.
5. Sticky bit / ACLs no implementados.

## 9. Consejos de depuración

- Comandos debug `whoami`, `id` muestran ids efectivos.
- `-EACCES` en traverse: falta execute en componente directorio.
- `-EPERM` en mount/chown: necesita euid root.

## 10. Roadmap futuro

- Conjunto capabilities y paridad `setresuid`.
- Base de datos `/etc/passwd`, contraseñas shadow.
- Bit setuid en exec.
- ACLs en tmpfs/minix (notado en IR0-vfs §10).
- Permisos de envío de señales.
- Namespaces — no planificado.

Ver: `IR0-vfs`, `IR0-process`, `IR0-syscalls`.
