#pragma once

#include <stdint.h>
#include <stdbool.h>

// ===============================================================================
// AUTHENTICATION SYSTEM HEADER
// ===============================================================================

// Authentication result codes
typedef enum
{
    AUTH_SUCCESS = 0,
    AUTH_INVALID_CREDENTIALS = 1,
    AUTH_TOO_MANY_ATTEMPTS = 2,
    AUTH_SYSTEM_LOCKED = 3
} auth_result_t;

// User credentials structure
typedef struct
{
    char username[32];
    char password[64]; // For future password support
    uint32_t uid;
    uint32_t gid;
    uint32_t permissions;
} user_credentials_t;

// Authentication configuration
typedef struct
{
    int max_attempts;
    int lockout_time;
    bool require_password;
    bool case_sensitive;
} auth_config_t;

// ===============================================================================
// FUNCTION DECLARATIONS
// ===============================================================================

/**
 * Initialize the authentication system
 * @param config Authentication configuration
 * @return 0 on success, -1 on failure
 */
int auth_init(auth_config_t *config);

/**
 * Main kernel login function
 * @return auth_result_t indicating the result
 */
auth_result_t kernel_login(void);

/**
 * Authenticate a user with username only
 * @param username Username to authenticate
 * @return auth_result_t indicating the result
 */
auth_result_t auth_user_simple(const char *username);

/**
 * Authenticate a user with username and password
 * @param username Username to authenticate
 * @param password Password to authenticate
 * @return auth_result_t indicating the result
 */
auth_result_t auth_user_full(const char *username, const char *password);

/**
 * Get the current authenticated user
 * @return Pointer to user credentials, NULL if not authenticated
 */
user_credentials_t *auth_get_current_user(void);

/**
 * Logout the current user
 */
void auth_logout(void);

/**
 * Check if system is locked due to failed attempts
 * @return true if locked, false otherwise
 */
bool auth_is_system_locked(void);

/**
 * Reset authentication attempts counter
 */
void auth_reset_attempts(void);
