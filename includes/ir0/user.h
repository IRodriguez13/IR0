#ifndef _IR0_USER_H
#define _IR0_USER_H

#include <stdint.h>

// User ID type
typedef uint32_t uid_t;

// Structure to hold user information
typedef struct {
    uid_t uid;        // User ID
    char name[32];    // Username
    char home[256];   // Home directory
    char shell[64];   // Login shell
} user_info_t;

// Get information about the current user
int get_current_user(user_info_t *user);

// Initialize the user subsystem
void user_init(void);

#endif /* _IR0_USER_H */