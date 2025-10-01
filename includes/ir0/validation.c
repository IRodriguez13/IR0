#include "validation.h"
#include "logging.h"
#include "string.h"

// ===============================================================================
// CONSTANTS
// ===============================================================================

#define MAX_FD_COUNT 1024
#define MAX_PATH_LENGTH 4096
#define MAX_BUFFER_SIZE (1024 * 1024) // 1MB

// ===============================================================================
// VALIDATION FUNCTIONS
// ===============================================================================

int validate_pointer(const void *ptr, const char *context)
{
    if (ptr == NULL)
    {
        log_error_fmt("VALIDATION", "NULL pointer in %s", context);
        return VALIDATION_ERROR_NULL_POINTER;
    }
    return VALIDATION_SUCCESS;
}

int validate_string(const char *str, const char *context)
{
    if (str == NULL)
    {
        log_error_fmt("VALIDATION", "NULL string in %s", context);
        return VALIDATION_ERROR_NULL_POINTER;
    }


    // Check for null-terminated string
    size_t len = strlen(str);
    if (len > MAX_PATH_LENGTH)
    {
        log_error_fmt("VALIDATION", "String too long in %s (length: %zu)", context, len);
        return VALIDATION_ERROR_INVALID_SIZE;
    }


    return VALIDATION_SUCCESS;
}

int validate_buffer(const void *buf, size_t size, const char *context)
{
    if (buf == NULL)
    {
        log_error_fmt("VALIDATION", "NULL buffer in %s", context);
        return VALIDATION_ERROR_NULL_POINTER;
    }

    if (size == 0)
    {
        log_error_fmt("VALIDATION", "Zero size buffer in %s", context);
        return VALIDATION_ERROR_INVALID_SIZE;
    }

    if (size > MAX_BUFFER_SIZE)
    {
        log_error_fmt("VALIDATION", "Buffer too large in %s (size: %zu)", context, size);
        return VALIDATION_ERROR_INVALID_SIZE;
    }


    return VALIDATION_SUCCESS;
}

int validate_range(int value, int min, int max, const char *context)
{
    if (value < min || value > max)
    {
        log_error_fmt("VALIDATION", "Value out of range in %s: %d (min: %d, max: %d)",
                      context, value, min, max);
        return VALIDATION_ERROR_INVALID_RANGE;
    }
    return VALIDATION_SUCCESS;
}

int validate_range_unsigned(size_t value, size_t min, size_t max, const char *context)
{
    if (value < min || value > max)
    {
        log_error_fmt("VALIDATION", "Value out of range in %s: %zu (min: %zu, max: %zu)",
                      context, value, min, max);
        return VALIDATION_ERROR_INVALID_RANGE;
    }
    return VALIDATION_SUCCESS;
}

int validate_fd(int fd, const char *context)
{
    if (fd < 0 || fd >= MAX_FD_COUNT)
    {
        log_error_fmt("VALIDATION", "Invalid file descriptor in %s: %d", context, fd);
        return VALIDATION_ERROR_INVALID_FD;
    }
    return VALIDATION_SUCCESS;
}

int validate_path(const char *path, const char *context)
{
    if (path == NULL)
    {
        log_error_fmt("VALIDATION", "NULL path in %s", context);
        return VALIDATION_ERROR_NULL_POINTER;
    }

    if (strlen(path) == 0)
    {
        log_error_fmt("VALIDATION", "Empty path in %s", context);
        return VALIDATION_ERROR_INVALID_PATH;
    }

    if (strlen(path) > MAX_PATH_LENGTH)
    {
        log_error_fmt("VALIDATION", "Path too long in %s", context);
        return VALIDATION_ERROR_INVALID_PATH;
    }


    // Check for path traversal attempts
    if (strstr(path, "..") != NULL)
    {
        log_warn_fmt("VALIDATION", "Path traversal attempt in %s: %s", context, path);
        return VALIDATION_ERROR_INVALID_PATH;
    }


    return VALIDATION_SUCCESS;
}

int validate_path_length(const char *path, size_t max_length, const char *context)
{
    if (path == NULL)
    {
        log_error_fmt("VALIDATION", "NULL path in %s", context);
        return VALIDATION_ERROR_NULL_POINTER;
    }


    size_t len = strlen(path);
    if (len > max_length)
    {
        log_error_fmt("VALIDATION", "Path too long in %s (length: %zu, max: %zu)",
                      context, len, max_length);
        return VALIDATION_ERROR_INVALID_SIZE;
    }


    return VALIDATION_SUCCESS;
}

int validate_memory_access(const void *ptr, size_t size, const char *context)
{
    if (ptr == NULL)
    {
        log_error_fmt("VALIDATION", "NULL pointer in %s", context);
        return VALIDATION_ERROR_NULL_POINTER;
    }

    if (size == 0)
    {
        log_error_fmt("VALIDATION", "Zero size memory access in %s", context);
        return VALIDATION_ERROR_INVALID_SIZE;
    }


    // TODO: Add memory boundary checks
    // This would require knowledge of memory layout


    return VALIDATION_SUCCESS;
}

int validate_user_buffer(const void *buf, size_t size, const char *context)
{
    if (buf == NULL)
    {
        log_error_fmt("VALIDATION", "NULL user buffer in %s", context);
        return VALIDATION_ERROR_NULL_POINTER;
    }

    if (size == 0)
    {
        log_error_fmt("VALIDATION", "Zero size user buffer in %s", context);
        return VALIDATION_ERROR_INVALID_SIZE;
    }

    if (size > MAX_BUFFER_SIZE)
    {
        log_error_fmt("VALIDATION", "User buffer too large in %s (size: %zu)", context, size);
        return VALIDATION_ERROR_INVALID_SIZE;
    }


    // TODO: Add user space validation
    // This would require checking if the buffer is in user space


    return VALIDATION_SUCCESS;
}

int validate_permissions(int flags, int allowed_flags, const char *context)
{
    if ((flags & ~allowed_flags) != 0)
    {
        log_error_fmt("VALIDATION", "Invalid flags in %s: 0x%x (allowed: 0x%x)",
                      context, flags, allowed_flags);
        return VALIDATION_ERROR_INVALID_FLAGS;
    }
    return VALIDATION_SUCCESS;
}

// ===============================================================================
// ERROR HANDLING
// ===============================================================================

const char *validation_error_string(validation_error_t error)
{
    switch (error)
    {
    case VALIDATION_SUCCESS:
        return "Success";
    case VALIDATION_ERROR_NULL_POINTER:
        return "Null pointer";
    case VALIDATION_ERROR_INVALID_RANGE:
        return "Invalid range";
    case VALIDATION_ERROR_INVALID_SIZE:
        return "Invalid size";
    case VALIDATION_ERROR_INVALID_FLAGS:
        return "Invalid flags";
    case VALIDATION_ERROR_PERMISSION_DENIED:
        return "Permission denied";
    case VALIDATION_ERROR_INVALID_PATH:
        return "Invalid path";
    case VALIDATION_ERROR_BUFFER_OVERFLOW:
        return "Buffer overflow";
    case VALIDATION_ERROR_INVALID_FD:
        return "Invalid file descriptor";
    default:
        return "Unknown error";
    }
}

void validation_log_error(validation_error_t error, const char *context, const char *operation)
{
    log_error_fmt("VALIDATION", "%s failed in %s: %s", operation, context, validation_error_string(error));
}
