#include "logging.h"
#include "vga.h"
#include "string.h"
#include <stdbool.h>
#include <stdarg.h>
#include <drivers/serial/serial.h>
#include <drivers/timer/clock_system.h>
#include <ir0/memory/kmem.h>

/* LOG BUFFER CONFIGURATION */
#define LOG_BUFFER_MAX_ENTRIES 1024  // Maximum number of log entries to store
#define LOG_BUFFER_ENTRY_SIZE  256   // Maximum size per log entry

/* LOG ENTRY STRUCTURE */
typedef struct {
    uint64_t timestamp_ms;      // Timestamp in milliseconds
    log_level_t level;           // Log level
    char component[32];          // Component name (truncated if needed)
    char message[LOG_BUFFER_ENTRY_SIZE - 32 - 8 - 4];  // Message text
} log_entry_t;

/* GLOBAL VARIABLES */

static log_level_t current_log_level = LOG_LEVEL_INFO;
static bool logging_initialized = false;

/* Circular log buffer */
static log_entry_t *log_buffer = NULL;
static size_t log_buffer_head = 0;    // Next position to write
static size_t log_buffer_count = 0;   // Number of entries in buffer
static bool log_buffer_wrapped = false; // True if buffer has wrapped around

/* INTERNAL FUNCTIONS */

static const char *get_level_string(log_level_t level)
{
    switch (level)
    {
    case LOG_LEVEL_DEBUG:
        return "DEBUG";
    case LOG_LEVEL_INFO:
        return "INFO";
    case LOG_LEVEL_WARN:
        return "WARN";
    case LOG_LEVEL_ERROR:
        return "ERROR";
    case LOG_LEVEL_FATAL:
        return "FATAL";
    default:
        return "UNKNOWN";
    }
}

/* Helper function to format and print timestamp to VGA */
static void print_timestamp(void)
{
    uint64_t uptime_ms = 0;
    
    /* Get uptime if clock system is initialized */
    /* clock_system_init() is called after logging_init(), so initially this will be 0 */
    /* Once clock is initialized, timestamps will be accurate */
    uptime_ms = clock_get_uptime_milliseconds();
    
    /* Format: [SSSS.mmm] where S = seconds, m = milliseconds */
    uint64_t seconds = uptime_ms / 1000;
    uint32_t milliseconds = (uint32_t)(uptime_ms % 1000);
    
    print("[");
    print_uint64(seconds);
    print(".");
    
    /* Print milliseconds with leading zeros (3 digits) */
    char ms_str[4];
    ms_str[0] = '0' + (char)((milliseconds / 100) % 10);
    ms_str[1] = '0' + (char)((milliseconds / 10) % 10);
    ms_str[2] = '0' + (char)(milliseconds % 10);
    ms_str[3] = '\0';
    
    print(ms_str);
    print("] ");
}

/* Helper function to format and print timestamp to serial */
static void serial_print_timestamp(void)
{
    uint64_t uptime_ms = 0;
    
    /* Get uptime if clock system is initialized */
    uptime_ms = clock_get_uptime_milliseconds();
    
    /* Format: [SSSS.mmm] where S = seconds, m = milliseconds */
    uint64_t seconds = uptime_ms / 1000;
    uint32_t milliseconds = (uint32_t)(uptime_ms % 1000);
    
    /* Convert seconds to string manually (simple approach for uint64_t) */
    char sec_buffer[32];
    uint64_t sec_temp = seconds;
    int sec_len = 0;
    
    if (sec_temp == 0) 
    {
        sec_buffer[sec_len++] = '0';
    } 
    else 
    {
        /* Convert to string in reverse */
        char rev_buffer[32];
        int rev_len = 0;
        
        while (sec_temp > 0) 
        {
            rev_buffer[rev_len++] = '0' + (char)(sec_temp % 10);
            sec_temp /= 10;
        }
        /* Reverse it */
        for (int i = rev_len - 1; i >= 0; i--) 
        {
            sec_buffer[sec_len++] = rev_buffer[i];
        }
    }
    sec_buffer[sec_len] = '\0';
    
    serial_print("[");
    serial_print(sec_buffer);
    serial_print(".");
    
    /* Print milliseconds with leading zeros (3 digits) */
    char ms_str[4];
    ms_str[0] = '0' + (char)((milliseconds / 100) % 10);
    ms_str[1] = '0' + (char)((milliseconds / 10) % 10);
    ms_str[2] = '0' + (char)(milliseconds % 10);
    ms_str[3] = '\0';
    
    serial_print(ms_str);
    serial_print("] ");
}

/* PUBLIC FUNCTIONS */

void logging_init(void)
{
    if (logging_initialized)
    {
        return;
    }

    /* Allocate log buffer */
    size_t buffer_size = sizeof(log_entry_t) * LOG_BUFFER_MAX_ENTRIES;
    log_buffer = (log_entry_t *)kmalloc(buffer_size);
    if (!log_buffer)
    {
        /* If allocation fails, logging will still work but buffer won't */
        log_buffer = NULL;
        /* Log to serial that buffer allocation failed */
        serial_print("[LOGGING] Warning: Failed to allocate log buffer (size: ");
        char buf[32];
        itoa((int)buffer_size, buf, 10);
        serial_print(buf);
        serial_print(" bytes)\n");
    }
    else
    {
        memset(log_buffer, 0, buffer_size);
        log_buffer_head = 0;
        log_buffer_count = 0;
        log_buffer_wrapped = false;
        serial_print("[LOGGING] Log buffer allocated successfully (");
        char buf[32];
        itoa(LOG_BUFFER_MAX_ENTRIES, buf, 10);
        serial_print(buf);
        serial_print(" entries)\n");
    }

    current_log_level = LOG_LEVEL_INFO;
    logging_initialized = true;
}

void logging_set_level(log_level_t level)
{
    current_log_level = level;
}

log_level_t logging_get_level(void)
{
    return current_log_level;
}

void log_message(log_level_t level, const char *component, const char *message)
{
    if (!logging_initialized)
    {
        logging_init();
    }

    if (level < current_log_level)
    {
        return;
    }

    /* Store log entry in circular buffer */
    if (log_buffer)
    {
        log_entry_t *entry = &log_buffer[log_buffer_head];
        entry->timestamp_ms = clock_get_uptime_milliseconds();
        entry->level = level;
        
        /* Copy component name (truncate if too long) */
        size_t comp_len = strlen(component);
        if (comp_len >= sizeof(entry->component))
            comp_len = sizeof(entry->component) - 1;
        memcpy(entry->component, component, comp_len);
        entry->component[comp_len] = '\0';
        
        /* Copy message (truncate if too long) */
        size_t msg_len = strlen(message);
        size_t max_msg_len = sizeof(entry->message) - 1;
        if (msg_len >= max_msg_len)
            msg_len = max_msg_len;
        memcpy(entry->message, message, msg_len);
        entry->message[msg_len] = '\0';
        
        /* Advance buffer head */
        log_buffer_head = (log_buffer_head + 1) % LOG_BUFFER_MAX_ENTRIES;
        if (log_buffer_count < LOG_BUFFER_MAX_ENTRIES)
        {
            log_buffer_count++;
        }
        else
        {
            log_buffer_wrapped = true;
        }
    }

    /* Only print to VGA for WARN, ERROR, and FATAL levels to reduce clutter */
    /* INFO and DEBUG go only to serial */
    if (level >= LOG_LEVEL_WARN)
    {
        print_timestamp();
        print("[");
        print(get_level_string(level));
        print("] [");
        print(component);
        print("] ");
        print(message);
        print("\n");
    }

    /* Always output to serial for debugging (with timestamp) */
    serial_print_timestamp();
    serial_print("[");
    serial_print(get_level_string(level));
    serial_print("] [");
    serial_print(component);
    serial_print("] ");
    serial_print(message);
    serial_print("\n");
}

void log_debug(const char *component, const char *message)
{
    log_message(LOG_LEVEL_DEBUG, component, message);
}

void log_info(const char *component, const char *message)
{
    log_message(LOG_LEVEL_INFO, component, message);
}

void log_warn(const char *component, const char *message)
{
    log_message(LOG_LEVEL_WARN, component, message);
}

void log_error(const char *component, const char *message)
{
    log_message(LOG_LEVEL_ERROR, component, message);
}

void log_fatal(const char *component, const char *message)
{
    log_message(LOG_LEVEL_FATAL, component, message);
}

/* =============================================================================== */
/* FORMATTED LOGGING FUNCTIONS */
/* =============================================================================== */

void log_debug_fmt(const char *component, const char *format, ...)
{
    if (!logging_initialized)
    {
        logging_init();
    }

    if (LOG_LEVEL_DEBUG < current_log_level)
    {
        return;
    }

    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    log_message(LOG_LEVEL_DEBUG, component, message);
}

void log_info_fmt(const char *component, const char *format, ...)
{
    if (!logging_initialized)
    {
        logging_init();
    }

    if (LOG_LEVEL_INFO < current_log_level)
    {
        return;
    }

    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    log_message(LOG_LEVEL_INFO, component, message);
}

void log_warn_fmt(const char *component, const char *format, ...)
{
    if (!logging_initialized)
    {
        logging_init();
    }

    if (LOG_LEVEL_WARN < current_log_level)
    {
        return;
    }

    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    log_message(LOG_LEVEL_WARN, component, message);
}

void log_error_fmt(const char *component, const char *format, ...)
{
    if (!logging_initialized)
    {
        logging_init();
    }

    if (LOG_LEVEL_ERROR < current_log_level)
    {
        return;
    }

    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    log_message(LOG_LEVEL_ERROR, component, message);
}

void log_fatal_fmt(const char *component, const char *format, ...)
{
    if (!logging_initialized)
    {
        logging_init();
    }

    if (LOG_LEVEL_FATAL < current_log_level)
    {
        return;
    }

    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    log_message(LOG_LEVEL_FATAL, component, message);
}

/* =============================================================================== */
/* SYSTEM-SPECIFIC LOGGING */
/* =============================================================================== */

void log_syscall(const char *syscall, int result, const char *args)
{
    char message[256];
    snprintf(message, sizeof(message), "SYSCALL: %s(%s) = %d", syscall, args ? args : "", result);
    log_debug("SYSCALL", message);
}

void log_filesystem_op(const char *op, const char *path, int result)
{
    char message[256];
    snprintf(message, sizeof(message), "FS: %s('%s') = %d", op, path ? path : "", result);
    log_debug("FILESYSTEM", message);
}

void log_memory_op(const char *op, void *ptr, size_t size, int result)
{
    char message[256];
    snprintf(message, sizeof(message), "MEM: %s(ptr=%p, size=%zu) = %d", op, ptr, size, result);
    log_debug("MEMORY", message);
}

void log_interrupt(uint8_t irq, const char *handler, int result)
{
    char message[256];
    snprintf(message, sizeof(message), "IRQ: %d handled by %s = %d", irq, handler ? handler : "unknown", result);
    log_debug("INTERRUPT", message);
}

void log_subsystem_ok(const char *subsystem_name)
{
    log_info(subsystem_name, "Registered and Initialized OK");
}

/* =============================================================================== */
/* LOG BUFFER ACCESS FUNCTIONS (for dmesg/journalctl-like functionality) */
/* =============================================================================== */

/**
 * logging_print_buffer - Print all logs from the circular buffer
 * 
 * This function prints all log entries stored in the circular buffer,
 * similar to dmesg or journalctl. Logs are printed in chronological order.
 */
void logging_print_buffer(void)
{
    if (!log_buffer)
    {
        print("Log buffer not allocated (no memory available)\n");
        return;
    }
    
    if (log_buffer_count == 0)
    {
        print("No log entries available (buffer is empty)\n");
        print("Buffer initialized: ");
        print(logging_initialized ? "yes" : "no");
        print("\n");
        return;
    }

    size_t start_idx;
    size_t count = log_buffer_count;
    
    /* Determine start index */
    if (log_buffer_wrapped)
    {
        /* Buffer has wrapped, start from head (oldest entry) */
        start_idx = log_buffer_head;
    }
    else
    {
        /* Buffer hasn't wrapped, start from beginning */
        start_idx = 0;
    }

    /* Print header */
    print("=== Kernel Log Buffer (dmesg) ===\n");
    print("Entries: ");
    char buf[32];
    itoa((int)log_buffer_count, buf, 10);
    print(buf);
    print("\n");
    print("-----------------------------------\n");

    /* Print all entries in chronological order */
    for (size_t i = 0; i < count; i++)
    {
        size_t idx = (start_idx + i) % LOG_BUFFER_MAX_ENTRIES;
        log_entry_t *entry = &log_buffer[idx];
        
        /* Format timestamp: [SSSS.mmm] */
        uint64_t seconds = entry->timestamp_ms / 1000;
        uint32_t milliseconds = (uint32_t)(entry->timestamp_ms % 1000);
        
        print("[");
        print_uint64(seconds);
        print(".");
        
        /* Print milliseconds with leading zeros (3 digits) */
        char ms_str[4];
        ms_str[0] = '0' + (char)((milliseconds / 100) % 10);
        ms_str[1] = '0' + (char)((milliseconds / 10) % 10);
        ms_str[2] = '0' + (char)(milliseconds % 10);
        ms_str[3] = '\0';
        print(ms_str);
        print("] ");
        
        /* Print level */
        print("[");
        print(get_level_string(entry->level));
        print("] ");
        
        /* Print component */
        print("[");
        print(entry->component);
        print("] ");
        
        /* Print message */
        print(entry->message);
        print("\n");
    }
}

/**
 * logging_get_buffer_size - Get number of log entries in buffer
 * @return: Number of log entries currently stored
 */
size_t logging_get_buffer_size(void)
{
    return log_buffer_count;
}
