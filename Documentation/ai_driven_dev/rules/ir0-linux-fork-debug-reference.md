<!-- IR0 AI dev rule: ir0-linux-fork-debug-reference -->
<!-- alwaysApply: false -->
<!-- description: Depuración kernel seria — árbol Linux local forkeado + historial de parches antes de parchear IR0 -->

# IR0 — Referencia Linux local y arqueología de parches

Para bugs **serios** (segfault, `#PF` con `cs=8`, entrega de señales, wait/TTY, uaccess,
syscall resume corrupto): **no parchear IR0 solo por intuición** tras el primer intento.

Complementa `oss-kernel-reference.md`, `linux-first-workflow.md` y
`linux-ground-truth-first.md`.

## Árbol de referencia (canónico del mantenedor)

| Variable | Default |
|----------|---------|
| `LINUX_TREE` | `~/Escritorio/linux` |

Override con env si el fork vive en otra ruta. Es **lectura + git log**; no copiar código
verbatim (licencia + facades IR0).

## Workflow obligatorio (después de reproducir en IR0)

1. **Síntoma IR0** — tags serial (`KERNEL_UACCESS_FAULT`, `DELIVER_DEFER`, `USER_FAULT_FRAME`),
   `comm`, `rip`, syscall bloqueada, secuencia userspace (pipeline, `^C`, `wait4`, etc.).
2. **Subsistema** — señales, MM fault, TTY, wait, syscall entry/exit (mapear a `kernel-maintainer/*`).
3. **Análogo Linux** — `Read`/`Grep` en `$LINUX_TREE`:
   - señales: `kernel/signal.c`, `arch/x86/kernel/signal.c`, `force_sig*`, `get_signal`
   - faults: `arch/x86/mm/fault.c`, `force_sig_fault`
   - traps: `arch/x86/kernel/traps.c`
   - TTY/job control: `drivers/tty/`, `kernel/signal.c` (`dequeue_signal`, `signal_setup`)
4. **Orden e invariantes** — qué chequea Linux antes de entregar/forzar/kill; qué difiere
   de IR0 (defer, `rip=0`, CR3, máscara, `kernel_syscall_sleep`).
5. **Historial de parches** — en `$LINUX_TREE`:
   ```bash
   cd "$LINUX_TREE"
   git log -S 'symbol_or_string' --oneline -- kernel/signal.c arch/x86/mm/fault.c
   git log --grep='keywords' --oneline --since='2018-01-01'
   git show <commit>   # fix mínimo + commit message = causa raíz
   ```
   Buscar fixes de la **misma clase** (signal during syscall, stale frame, PF in uaccess,
   SIGSEGV recursion). Citar `commit` + asunto en informe oleada / cuerpo de MR.
6. **Fix IR0 mínimo** — alinear semántica con Linux; respetar facades; comentario corto si
   hay divergencia deliberada (`ir0-userspace-first-linux-abi.md`).
7. **Validar** — smokes que ejerciten el path manual (`ir0-smoke-behavior-validation.md`) +
   gates CTR.

## Cuándo aplica

- Segfault / panic userspace o kernel en consola, pipes, señales, wait, exec.
- Divergencia ABI donde Linux + man definen comportamiento observable.
- Segundo intento fallido sin haber leído el análogo Linux.

## Anti-patrones

- Parchear IR0 tras un solo `Read` sin comparar orden Linux ni buscar commits relacionados.
- Copiar bloques Linux sin adaptar facades / `copy_*_user` / locking IR0.
- Cerrar oleada porque el smoke imprime tag de éxito **después** de un fatal en el log.
- Parchear BusyBox/ash antes del kernel para semántica signal/TTY (`ir0-userspace-first-linux-abi.md`).

## Salida mínima antes de codificar

- subsistema + síntoma IR0 (1–3 líneas)
- anclas Linux: `path:symbol` (≥2 si el bug cruza subsistemas)
- commit(s) Linux consultados (hash corto + tema) o «ninguno encontrado»
- primera divergencia observable IR0 vs Linux
- fix propuesto (1 frase)
