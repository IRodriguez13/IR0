# IR0 y Semantica Unix-Like

Este documento define limites practicos de compatibilidad, no comparativas historicas.

## Donde IR0 Alinea Bien

- Modelo de syscalls tipo Unix para operaciones base de archivo/proceso.
- Interaccion por paths con VFS y pseudo-filesystems de introspeccion.
- Campos y flujo de ownership/permisos (`uid/gid/euid/egid`, `umask`).
- Workflow shell-style con debug bins sobre interfaz por syscalls.

## Donde IR0 Difiere a Proposito

- Arquitectura debug-first con DebShell integrada para iteracion.
- Gating de subsistemas por Kconfig directo al flujo build/runtime.
- Bootstrap y registry de drivers orientados a control de bring-up.
- Ruta liviana de cuentas/sudo (nivel MVP), no stack completo de auth.

## Limites Actuales de Compatibilidad

- El modelo de seguridad/cuentas no es aun un stack Unix full productivo.
- Profundidad de scheduler esta en fase de estabilizacion.
- Algunos casos borde POSIX estan parciales o en evolucion.

## Puntos Fuertes

- Modularidad arquitectonica y desacople de subsistemas.
- Loop rapido de iteracion por tooling + observabilidad runtime.

## Puntos Debiles

- Varias features de compatibilidad estan en implementacion minima viable.
- El comportamiento de cola larga requiere mas escenarios de regresion reales.
