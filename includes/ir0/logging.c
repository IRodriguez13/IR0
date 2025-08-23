#include "logging.h"
#include "print.h"
#include "string.h"
#include <stdbool.h>
#include <stdarg.h>

// ===============================================================================
// GLOBAL VARIABLES
// ===============================================================================

static log_level_t current_log_level = LOG_LEVEL_INFO;
static bool logging_initialized = false;

// ===============================================================================
// INTERNAL FUNCTIONS
// ===============================================================================

static const char* get_level_string(log_level_t level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_ERROR: return "ERROR";
        case LOG_LEVEL_FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

static void print_timestamp(void) {
    // TODO: Implement real timestamp
    print("[");
    print_int32(0); // Placeholder for timestamp
    print("] ");
}

// ===============================================================================
// PUBLIC FUNCTIONS
// ===============================================================================

void logging_init(void) {
    if (logging_initialized) {
        return;
    }
    
    current_log_level = LOG_LEVEL_INFO;
    logging_initialized = true;
    
    print("Logging system initialized\n");
}

void logging_set_level(log_level_t level) {
    current_log_level = level;
}

log_level_t logging_get_level(void) {
    return current_log_level;
}

void log_message(log_level_t level, const char *component, const char *message) {
    if (!logging_initialized) {
        logging_init();
    }
    
    if (level < current_log_level) {
        return;
    }
    
    print_timestamp();
    print("[");
    print(get_level_string(level));
    print("] [");
    print(component);
    print("] ");
    print(message);
    print("\n");
}

void log_debug(const char *component, const char *message) {
    log_message(LOG_LEVEL_DEBUG, component, message);
}

void log_info(const char *component, const char *message) {
    log_message(LOG_LEVEL_INFO, component, message);
}

void log_warn(const char *component, const char *message) {
    log_message(LOG_LEVEL_WARN, component, message);
}

void log_error(const char *component, const char *message) {
    log_message(LOG_LEVEL_ERROR, component, message);
}

void log_fatal(const char *component, const char *message) {
    log_message(LOG_LEVEL_FATAL, component, message);
}

// ===============================================================================
// FORMATTED LOGGING FUNCTIONS
// ===============================================================================

void log_debug_fmt(const char *component, const char *format, ...) {
    if (!logging_initialized) {
        logging_init();
    }
    
    if (LOG_LEVEL_DEBUG < current_log_level) {
        return;
    }
    
    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    log_message(LOG_LEVEL_DEBUG, component, message);
}

void log_info_fmt(const char *component, const char *format, ...) {
    if (!logging_initialized) {
        logging_init();
    }
    
    if (LOG_LEVEL_INFO < current_log_level) {
        return;
    }
    
    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    log_message(LOG_LEVEL_INFO, component, message);
}

void log_warn_fmt(const char *component, const char *format, ...) {
    if (!logging_initialized) {
        logging_init();
    }
    
    if (LOG_LEVEL_WARN < current_log_level) {
        return;
    }
    
    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    log_message(LOG_LEVEL_WARN, component, message);
}

void log_error_fmt(const char *component, const char *format, ...) {
    if (!logging_initialized) {
        logging_init();
    }
    
    if (LOG_LEVEL_ERROR < current_log_level) {
        return;
    }
    
    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    log_message(LOG_LEVEL_ERROR, component, message);
}

void log_fatal_fmt(const char *component, const char *format, ...) {
    if (!logging_initialized) {
        logging_init();
    }
    
    if (LOG_LEVEL_FATAL < current_log_level) {
        return;
    }
    
    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    log_message(LOG_LEVEL_FATAL, component, message);
}

// ===============================================================================
// SYSTEM-SPECIFIC LOGGING
// ===============================================================================

void log_syscall(const char *syscall, int result, const char *args) {
    char message[256];
    snprintf(message, sizeof(message), "SYSCALL: %s(%s) = %d", syscall, args ? args : "", result);
    log_debug("SYSCALL", message);
}

void log_filesystem_op(const char *op, const char *path, int result) {
    char message[256];
    snprintf(message, sizeof(message), "FS: %s('%s') = %d", op, path ? path : "", result);
    log_debug("FILESYSTEM", message);
}

void log_memory_op(const char *op, void *ptr, size_t size, int result) {
    char message[256];
    snprintf(message, sizeof(message), "MEM: %s(ptr=%p, size=%zu) = %d", op, ptr, size, result);
    log_debug("MEMORY", message);
}

void log_interrupt(uint8_t irq, const char *handler, int result) {
    char message[256];
    snprintf(message, sizeof(message), "IRQ: %d handled by %s = %d", irq, handler ? handler : "unknown", result);
    log_debug("INTERRUPT", message);
}
