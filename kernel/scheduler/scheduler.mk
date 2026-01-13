# Scheduler configuration
# This file defines the available schedulers and their properties
#
# IMPORTANTE: Este sistema requiere que registres manualmente los schedulers.
# NO detecta automáticamente archivos por nombre, debes especificar cada uno.
#
# Para agregar un nuevo scheduler:
# 1. Agrega una línea al SCHEDULER_REGISTRY con formato: NOMBRE|nombre_corto|ruta_archivo
# 2. El sistema verificará si el archivo fuente existe
# 3. Solo los schedulers con archivos fuente existentes estarán disponibles para compilar
#
# Ejemplo: Si tienes un archivo kernel/mi_scheduler.c, agrega:
#   MI_SCHEDULER|mi|kernel/mi_scheduler.c

# Registro de schedulers (debes especificar manualmente cada uno)
# Formato: NOMBRE_MAYUSCULAS|nombre_minusculas|ruta/archivo_fuente.c
SCHEDULER_REGISTRY := \
	ROUND_ROBIN|rr|kernel/rr_sched.c \
	CFS|cfs|kernel/cfs_sched.c \
	PRIORITY|priority|kernel/priority_sched.c

# Initialize empty lists
AVAILABLE_SCHEDULERS :=
AVAILABLE_SCHEDULERS_NAMES :=
SCHEDULER_SOURCES :=

# Process each scheduler in the registry
define check_scheduler
	$(eval SCHED_INFO := $(subst |, ,$(1)))
	$(eval SCHED_NAME := $(word 1,$(SCHED_INFO)))
	$(eval SCHED_SHORT := $(word 2,$(SCHED_INFO)))
	$(eval SCHED_FILE := $(word 3,$(SCHED_INFO)))
	$(if $(wildcard $(KERNEL_ROOT)/$(SCHED_FILE)),\
		$(eval AVAILABLE_SCHEDULERS += $(SCHED_NAME)) \
		$(eval AVAILABLE_SCHEDULERS_NAMES += $(SCHED_SHORT)) \
		$(eval SCHEDULER_SOURCES += $(SCHED_FILE)) \
	)
endef

# Check each scheduler
$(foreach sched,$(SCHEDULER_REGISTRY),$(eval $(call check_scheduler,$(sched))))

# Add header directory to include path
CFLAGS += -I$(KERNEL_ROOT)/kernel/scheduler

# Export variables to parent make
AVAILABLE_SCHEDULERS := $(AVAILABLE_SCHEDULERS)
AVAILABLE_SCHEDULERS_NAMES := $(AVAILABLE_SCHEDULERS_NAMES)
SCHEDULER_SOURCES := $(SCHEDULER_SOURCES)

# Add scheduler objects to KERNEL_OBJS (will be appended in parent Makefile after include)
SCHEDULER_OBJS := $(SCHEDULER_SOURCES:.c=.o)
