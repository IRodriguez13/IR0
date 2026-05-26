<!-- IR0 AI dev rule: ir0-development-plan-mode -->
<!-- alwaysApply: true -->
<!-- description: IR0 — cuándo y cómo usar Plan mode antes de implementar cambios amplios -->

# IR0 — Plan Mode Workflow

## Cuándo cambiar a Plan mode

Usa **Plan mode** (`SwitchMode` → `plan`) **antes de codificar** si ocurre cualquiera de:

- Alcance ≥2 subsistemas (p. ej. syscalls + procfs + arch).
- Refactor de facades, registry, Kconfig/Makefile o ABI.
- El usuario pide **modo plan**, **multiagentes**, **oleada**, o **endurecer patrón**.
- Hay trade-offs no obvios (COW vs copy, registry vs legacy, TCP vs ENOSYS).
- Una auditoría o evaluación por tier (T0–T3) precede a la implementación.

**No** uses Plan mode para fixes locales de un solo archivo con causa raíz clara.

## Flujo obligatorio en Plan mode

1. **Explorar** — `Read`/`Grep` y, si hace falta, 1–3 agentes `explore` en paralelo (solo lectura).
2. **Acotar** — `AskQuestion` si el foco (pattern-first vs full-stack, proc legacy, etc.) no está claro.
3. **Plan escrito** — `CreatePlan` con:
   - Objetivo y alcance explícito (in/out).
   - Mapa de capas o mermaid si ayuda.
   - Workstreams paralelos (agente A/B/C) con archivos concretos.
   - Orden de integración (ver regla multiagente).
   - Criterios de done + gates CTR.
4. **Esperar aprobación** del usuario antes de implementar.
5. **Implementar** en Agent mode con todos del plan (no re-planificar salvo bloqueo).

## Reglas del archivo de plan

- **No editar** el `.plan.md` generado por Cursor salvo petición explícita del usuario.
- Los TODOs del plan se marcan `in_progress` / `completed`; no duplicar listas.
- Si el plan queda obsoleto mid-flight, resumir desviación en el **informe de oleada**, no reescribir el plan.

## Research antes de especificar ABI

En Plan mode, para syscalls, ELF, input/fb uapi, USB/HCI, etc.:

1. `WebSearch` / `WebFetch` a fuente primaria (Linux uapi, musl, OSDev).
2. Verificar en árbol con `Grep`/`Read` — el README no es spec.
3. Citar fuente en el plan o en el informe de oleada.

## Salida esperada al cerrar el plan

Al terminar la implementación, entregar el **informe en formato oleada** (regla `ir0-development-multiagent-format.md`), no solo un diff.
