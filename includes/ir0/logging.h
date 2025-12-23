#pragma once

#include <stdint.h>
#include <stddef.h>

/*LOGGING SYSTEM*/ 

typedef enum 
{
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_ERROR = 3,
    LOG_LEVEL_FATAL = 4
} log_level_t;

/*LOGGING FUNCTIONS*/ 

/* BASIC LOGGING FUNCTIONS */ 
void log_message(log_level_t level, const char *component, const char *message);
void log_debug(const char *component, const char *message);
void log_info(const char *component, const char *message);
void log_warn(const char *component, const char *message);
void log_error(const char *component, const char *message);
void log_fatal(const char *component, const char *message);

/* FORMATTED LOGGING FUNCTIONS */ 
void log_debug_fmt(const char *component, const char *format, ...);
void log_info_fmt(const char *component, const char *format, ...);
void log_warn_fmt(const char *component, const char *format, ...);
void log_error_fmt(const char *component, const char *format, ...);
void log_fatal_fmt(const char *component, const char *format, ...);

/* SYSTEM-SPECIFIC LOGGING */ 
void log_syscall(const char *syscall, int result, const char *args);
void log_filesystem_op(const char *op, const char *path, int result);
void log_memory_op(const char *op, void *ptr, size_t size, int result);
void log_interrupt(uint8_t irq, const char *handler, int result);
void log_subsystem_ok(const char *subsystem_name);

/* LOGGING INITIALIZATION */ 
void logging_init(void);
void logging_set_level(log_level_t level);
log_level_t logging_get_level(void);

/* LOG BUFFER ACCESS (FOR DMESG/JOURNALCTL-LIKE FUNCTIONALITY) */ 
void logging_print_buffer(void);  // Print all logs in buffer
size_t logging_get_buffer_size(void);  // Get number of log entries in buffer

/* LOGGING MACROS */ 

#define LOG_DEBUG(component, message) log_debug(component, message)
#define LOG_INFO(component, message) log_info(component, message)
#define LOG_WARNING(component, message) log_warn(component, message)
#define LOG_ERROR(component, message) log_error(component, message)
#define LOG_FATAL(component, message) log_fatal(component, message)

#define LOG_DEBUG_FMT(component, format, ...) log_debug_fmt(component, format, ##__VA_ARGS__)
#define LOG_INFO_FMT(component, format, ...) log_info_fmt(component, format, ##__VA_ARGS__)
#define LOG_WARNING_FMT(component, format, ...) log_warn_fmt(component, format, ##__VA_ARGS__)
#define LOG_ERROR_FMT(component, format, ...) log_error_fmt(component, format, ##__VA_ARGS__)
#define LOG_FATAL_FMT(component, format, ...) log_fatal_fmt(component, format, ##__VA_ARGS__)

#define LOG_SYSCALL(syscall, result, args) log_syscall(syscall, result, args)
#define LOG_FS_OP(op, path, result) log_filesystem_op(op, path, result)
#define LOG_MEM_OP(op, ptr, size, result) log_memory_op(op, ptr, size, result)
#define LOG_IRQ(irq, handler, result) log_interrupt(irq, handler, result)
