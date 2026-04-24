# Scheduling en IR0

El scheduling en IR0 se selecciona por API de scheduler y politica desde Kconfig.

## Modelo de Seleccion

- Capa de dispatch: `kernel/scheduler_api.c`.
- Clave de seleccion: `CONFIG_SCHEDULER_POLICY`.
- Politicas conectadas hoy:
  - Round-robin.
  - Wrapper compatible con CFS (conservador).
  - Wrapper compatible con priority (conservador).

## Caracteristicas Runtime

- Integracion con flujo de procesos y senales.
- Mutaciones de cola endurecidas para mayor seguridad concurrente.
- Context-switch assembly especifico por arquitectura.

## Puntos Fuertes

- Seleccion de scheduler guiada por configuracion.
- Frontera de API facilita iterar politicas sin tocar todo el arbol.
- Mejoras de estabilidad redujeron riesgos comunes en colas.

## Puntos Debiles

- Las politicas alternativas aun son minimas y evolucionan.
- Fairness/latencia profunda siguen como trabajo futuro.
- Scheduling orientado a SMP no es baseline actual.
