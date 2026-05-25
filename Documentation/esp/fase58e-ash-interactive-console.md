# FASE58E — BusyBox ash interactivo en `/dev/console` (QEMU GTK)

**Estado:** Hecho (2026-05-25)  
**Tier:** T1 (userspace POSIX mínimo) — vertical slice  
**Depende de:** handoff de consola FASE58A, binding devfs de fd, irinit stdio

## Resumen

IR0 arranca **irinit** → **BusyBox ash** en la consola framebuffer. El usuario ve
el prompt `#`, **las teclas se muestran al escribir** y **Enter envía la línea**
al shell (p. ej. `ls`, `pwd`, `echo hi`).

Este era el último bloqueo para un shell interactivo real en QEMU GTK sin parchear
BusyBox para eco en userspace.

## Qué ve el usuario

1. Banner: `BusyBox v1.36.1 … built-in shell (ash)`
2. Prompt `#` en blanco (`0x0F`, stderr)
3. Caracteres visibles mientras se teclea (eco TTY del kernel, ICANON + ECHO)
4. Enter ejecuta el comando; stdout/stderr en el mismo FB

Comandos como `ps` pueden responder `not found` si no están en el BusyBox
mínimo fase50 — eso es configuración del applet, no un fallo de consola.

## Causas raíz corregidas

### 1. Sin poll de teclado con ash bloqueado en `read(0)`

Con todos los procesos bloqueados, el scheduler RR **no tiene hilo idle** todavía;
`kernel_idle_poll()` (y `keyboard_poll_ps2()`) no corría mientras ash esperaba en
`read(0)`.

En QEMU GTK a menudo llegan scancodes PS/2 por el puerto `0x60` **sin IRQ1
fiable**. El log serial no mostraba ningún `KBD_*` tras `SYS_READ_ENTER`.

**Arreglo:** llamar `kernel_idle_poll()` en el bucle de espera de
`tty_sleep_for_input()` y en `sys_poll()` bloqueante.

### 2. Eco TTY desacoplado de la pulsación de tecla

El path legacy `syscalls_read_stdio_stdin()` no pasaba por la disciplina TTY.

**Arreglo:** `ir0_console_read()` + `ir0_console_keypress()` en cada tecla;
eco inmediato al framebuffer; en ICANON no se llena el ring crudo del teclado.

## Ruta de datos

```
PS/2 → keyboard_poll_ps2 / IRQ1
     → ir0_console_keypress() → eco FB
     → (Enter) read(0) devuelve la línea a ash
```

En espera bloqueada: `sched_schedule_next()` → si sigue BLOCKED →
`kernel_idle_poll()`.

## Archivos principales

| Área | Archivo |
|------|---------|
| TTY / espera | `includes/ir0/console.c` |
| Teclado | `interrupt/arch/keyboard.c` |
| Syscalls | `kernel/syscalls.c` |
| PID 1 | `setup/pid1/irinit.c` |
| BusyBox | `setup/busybox/fase50_minimal.config` |
| Próximo BusyBox | `setup/busybox/fase58_busybox.config` (`make build-busybox-fase58-plus`) |

## BusyBox (config intencional)

```
CONFIG_ASH_JOB_CONTROL=n
CONFIG_FEATURE_EDITING=n
```

El kernel hace eco y línea canónica; no hace falta lineedit de BusyBox en esta fase.

## Compilar y ejecutar

```bash
make kernel-x64-userspace.iso build-irinit build-busybox-fase50-min
make run-fase58e-ash-gui
```

**Clic en la ventana QEMU** antes de teclear.

Log serial: `/tmp/fase58e-ash-gui.log` — `make check-fase58e-logs`

Reconstruir la **ISO** tras cambios en el kernel.

## Criterios de éxito

1. Prompt `#` visible
2. Letras visibles al teclear
3. `pwd` / `echo hi` con salida en pantalla
4. Serial: tags compactos FASE58K (`KBD_USER_POLL_OK`, `TTY_CANON_LINE_READY`, …)

Smoke automático (headless):

```bash
make smoke-fase58e-ash-interactive
make check-fase58e-logs
```

Si falla la inyección por monitor, usar GUI manual: `make run-fase58e-ash-gui`

Tags compactos (sin spam por carácter): `ASH_INTERACTIVE_READY`, `KBD_USER_POLL_OK`,
`TTY_CANON_LINE_READY`, `SYS_READ_RETURN_OK`, `ASH_COMMAND_ECHO_OK`,
`ASH_COMMAND_EXEC_OK`.

Si hay prompt pero no `KBD_USER_POLL_OK` al teclear → foco QEMU / input del host.

## Límites conocidos

- Sin hilo idle en el kernel: hay que llamar `kernel_idle_poll()` desde paths
  de bloqueo TTY/poll.
- BusyBox mínimo: solo applets del config fase50.
- Doom autostart desactivado en irinit producción.

## Documentación relacionada

- [fase58e (English)](../fase58e-ash-interactive-console.md)
- [Plan FASE57](../fase57-reintegration-plan.md)
