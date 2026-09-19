<!-- IR0 AI dev rule: ir0-smoke-behavior-validation -->
<!-- alwaysApply: false -->
<!-- description: Smokes y tests deben medir el comportamiento real del fix — no tags optimistas ni un solo trial -->

# IR0 — Validación de comportamiento (smokes y tests)

Un fix **no está cerrado** si el test solo comprueba que «algo verde apareció en serial».
Debe demostrar que el **síntoma manual** (`make run`, teclado real, pipeline + `^C`) no puede
reaparecer.

Complementa `ir0-smoke-autokill.md`, `ir0-unit-tests-policy.md`, `ir0-code-source-truth.md`.

## Principio

| Mal | Bien |
|-----|------|
| Un sendkey rápido + tag terminal | Misma secuencia que el reporte del usuario |
| PASS si el tag final existe en cualquier parte del log | FAIL si hay fatal **antes** del tag, aunque el shell reinicie |
| 1 trial en TTY/QEMU | ≥2–3 trials en smokes de teclado/login (flake real) |
| Host test que solo `assert(ret == 0)` | Assert errno, orden, side effects del contrato |

## Smokes QEMU (consola / TTY / señales)

1. **Path manual** — documentar en el harness la secuencia que reproduce el bug
   (firstboot, login, `./bin/true`, `hexdump \| grep`, `^C`, `wait`, etc.).
2. **Guards compartidos** — importar `scripts/smoke_tty_guards.py` para:
   - fatals: `KERNEL_UACCESS_FAULT`, `CONSOLE_SESSION_SEGV`, `USER_FAULT_FRAME`, …
   - corrupción TTY: non-ASCII en prompt, `Invalid argument` en comando ASCII, teclado duplicado
   - supervisor: `RUNSV_CONSOLE_START` mid-session, `DELIVER_CTX sig=17` a `run`
3. **Ventana de log** — aplicar guards sobre el tramo **desde el mark de sesión**, no solo
   al final; un `PIPESTDININTOK` tras `CONSOLE_SESSION_SEGV` sigue siendo FAIL.
4. **Multi-trial** — smokes de login/teclado: repetir wizard + login + comandos (p. ej.
   `smoke_firstboot_login_typing.py`: 3 trials).
5. **Autokill** — `$(SMOKE_QEMU_RUN)` con `--done`; timeout = techo, no runtime esperado
   (`ir0-smoke-autokill.md`).
6. **Baterías** — al endurecer un área (p. ej. `smoke-keyboard-stability`), incluir todos
   los smokes del cluster antes de declarar verde.

## Host / ktest / contratos ABI

- Assert **errno**, valor de retorno y estado observable (wait status, VMA, fd flags).
- Contrato Linux: workload + compare (`scripts/linux_abi/`) antes de marcar VERIFIED.
- Tras cambio kernel: `make -C tests/host run` del alcance + smoke del tier afectado.

## Criterio de cierre de fix

- [ ] Reproducción manual o smoke equivalente documentado.
- [ ] Log revisado sin fatals/guards en la ventana de la sesión.
- [ ] Multi-trial PASS donde el flake era conocido.
- [ ] Gates CTR verdes (`kernel-x64.bin`, `arch-guard`, `tests/host`).

## Anti-patrones

- «Pasa smoke» pero el mantenedor ve `êllss` o segfault en `make run`.
- Subir timeout en lugar de guards o secuencia más fiel.
- Tag de éxito emitido por shell **nuevo** tras crash del anterior contado como PASS.
- Host test nuevo que no fallaría si se revirtiera el fix.
