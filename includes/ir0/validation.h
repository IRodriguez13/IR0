#ifndef IR0_VALIDATION_H
#define IR0_VALIDATION_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ===============================================================================
// VALIDATION SYSTEM
// ===============================================================================

// Error codes for validation
typedef enum 
{
    VALIDATION_SUCCESS = 0,
    VALIDATION_ERROR_NULL_POINTER = -1,
    VALIDATION_ERROR_INVALID_RANGE = -2,
    VALIDATION_ERROR_INVALID_SIZE = -3,
    VALIDATION_ERROR_INVALID_FLAGS = -4,
    VALIDATION_ERROR_PERMISSION_DENIED = -5,
    VALIDATION_ERROR_INVALID_PATH = -6,
    VALIDATION_ERROR_BUFFER_OVERFLOW = -7,
    VALIDATION_ERROR_INVALID_FD = -8
} validation_error_t;

// ===============================================================================
// VALIDATION FUNCTIONS
// ===============================================================================

// Pointer validation
int validate_pointer(const void *ptr, const char *context);
int validate_string(const char *str, const char *context);
int validate_buffer(const void *buf, size_t size, const char *context);

// Range validation
int validate_range(int value, int min, int max, const char *context);
int validate_range_unsigned(size_t value, size_t min, size_t max, const char *context);
int validate_fd(int fd, const char *context);

// Path validation
int validate_path(const char *path, const char *context);
int validate_path_length(const char *path, size_t max_length, const char *context);

// Memory validation
int validate_memory_access(const void *ptr, size_t size, const char *context);
int validate_user_buffer(const void *buf, size_t size, const char *context);

// Permission validation
int validate_permissions(int flags, int allowed_flags, const char *context);

// ===============================================================================
// VALIDATION MACROS
// ===============================================================================

#define VALIDATE_POINTER(ptr, context) \
    do { \
        int result = validate_pointer(ptr, context); \
        if (result != VALIDATION_SUCCESS) return result; \
    } while(0)

#define VALIDATE_STRING(str, context) \
    do { \
        int result = validate_string(str, context); \
        if (result != VALIDATION_SUCCESS) return result; \
    } while(0)

#define VALIDATE_RANGE(value, min, max, context) \
    do { \
        int result = validate_range(value, min, max, context); \
        if (result != VALIDATION_SUCCESS) return result; \
    } while(0)

#define VALIDATE_FD(fd, context) \
    do { \
        int result = validate_fd(fd, context); \
        if (result != VALIDATION_SUCCESS) return result; \
    } while(0)

#define VALIDATE_PATH(path, context) \
    do { \
        int result = validate_path(path, context); \
        if (result != VALIDATION_SUCCESS) return result; \
    } while(0)

// ===============================================================================
// ERROR HANDLING
// ===============================================================================

const char* validation_error_string(validation_error_t error);
void validation_log_error(validation_error_t error, const char *context, const char *operation);

#endif // IR0_VALIDATION_H
