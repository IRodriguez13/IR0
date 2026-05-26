<!-- IR0 AI dev rule: ir0-development-multiagent-format -->
<!-- alwaysApply: true -->
<!-- description: IR0 — orquestación multiagente, oleadas e informe de progreso estándar -->

# IR0 — Multi-Agent Development Format

## Cuándo paralelizar

Lanza **2–4 agentes en paralelo** (`Task` en un mismo turno) cuando los workstreams son independientes:

| Agente | Rol | Entregable |
|--------|-----|------------|
| `explore` | Auditoría / gap analysis (readonly) | P0/P1 con rutas de archivo |
| `generalPurpose` | Implementación | Diff mínimo, facades intactas |
| `generalPurpose` | Tests + integración | ktest, host test, wiring |
| `shell` | Validación CTR | Salida de make/arch-guard |

**No** paralelizar si comparten el mismo archivo crítico sin coordinación (p. ej. `syscalls.c` + ABI entry en el mismo lote sin orden).

## Orden de integración (merge mental)

```
Kconfig/defconfig/Makefile
  → includes/ir0/* facades
  → kernel/fs/mm (portable)
  → arch/* + asm
  → drivers/*
  → tests (host, ktest, memsafe)
  → docs (solo si el usuario lo pidió; English en /Documentation)
```

Tras agentes paralelos, **un paso de integración** en el agente principal: resolver conflictos, un solo build matrix, un informe.

## Oleada (batch)

Una **oleada** = Plan aprobado → agentes paralelos → integración → gates CTR → informe.

- Dividir por **vertical slice** o subsistema, no por “todos los syscalls del universo”.
- Cada oleada debe cerrar con al menos: `make kernel-x64.bin` + `arch-guard`.
- Oleadas grandes: añadir `build-matrix-min` y `tests/host`; `kernel-tests` si hay ktest/QEMU tocados.

## Gates CTR (post-oleada)

```bash
make -s kernel-x64.bin
python3 scripts/architecture_guard.py
make -s build-matrix-min
make -s -C tests/host run
# make kernel-tests  — si hay ktest y disk.img libre
```

## Informe de progreso obligatorio (“mismo formato”)

Al cerrar una oleada o evaluación, usar **esta estructura** (español o inglés, consistente en la respuesta):

### 1. Cambios de esta oleada

Tabla: **Área | Cambio** (concreto, sin marketing).

### 2. Validación

Tabla: **Check | Resultado** (OK / FAIL / BLOQUEADO + motivo).

### 3. Evaluación por tier

Tabla: **Tier | Antes | Ahora | Notas** (T0–T3, % orientativos, honestos).

### 4. P0 / P1 / P2 restante

Listas priorizadas; no mezclar aspiracional con hecho.

### 5. Próximo paso recomendado

1–3 ítems ordenados; preguntar al usuario solo si el fork es real.

## Anti-patterns multiagente

- Varios agentes editando el mismo plan o regla duplicada sin necesidad.
- Crear markdown de documentación no pedido.
- Declarar tier “done” sin prueba runnable.
- Agentes que reportan “compila” sin ejecutar los makes anteriores.
- Duplicar registry y legacy sin documentar deuda en **Pendiente explícito**.

## Relación con otras rules

- Arquitectura y estilo: `kernel-architecture-rigor.md`, `Documentation/ai_driven_dev/skills/ctr/SKILL.md`.
- Research + tiers: `ir0-roadmap-research-multiagent.md`, `ir0-tier-t*.md`.
- Plan previo: `ir0-development-plan-mode.md`.
