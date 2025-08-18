// kernel/scheduler/scheduler.h - VERSIÓN CORREGIDA
#pragma once
#include "scheduler_types.h"

// ===============================================================================
// API PÚBLICA UNIFICADA (compatible con código existente)
// ===============================================================================

void scheduler_init(void);        // Inicializar sistema de schedulers
void scheduler_tick(void);        // Llamado desde timer interrupt
void add_task(task_t* task);      // Agregar tarea al scheduler activo
void scheduler_start(void);       // Iniciar ejecución de tareas
void scheduler_main_loop(void);   // Loop principal del scheduler - NUNCA RETORNA

// ===============================================================================
// NUEVAS FUNCIONES DE GESTIÓN
// ===============================================================================

void scheduler_dispatch_loop(void);              // Loop principal del dispatcher
scheduler_type_t get_active_scheduler(void);     // Obtener tipo de scheduler activo
const char* get_scheduler_name(void);            // Obtener nombre del scheduler
void force_scheduler_fallback(void);             // Forzar fallback manual

// ===============================================================================
// FUNCIONES DE DEBUG Y COMPATIBILIDAD
// ===============================================================================

void dump_scheduler_state(void);   // Mostrar estado del scheduler
int scheduler_ready(void);         // Verificar si scheduler está listo

// ===============================================================================
// PARCHE 4: Actualizar task.h - AGREGAR DEFINE FALTANTE
// ===============================================================================

// kernel/scheduler/task.h - AGREGAR ESTA LÍNEA AL FINAL
#ifndef MAX_TASKS
#define MAX_TASKS 256  // Máximo número de tareas para evitar loops infinitos
#endif