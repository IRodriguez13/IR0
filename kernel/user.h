#pragma once

#include <stdint.h>

#define MAX_PATH_LEN 256

typedef struct {
    uint32_t uid;
    uint32_t gid;
    char name[32];
    char home[MAX_PATH_LEN];
    char shell[MAX_PATH_LEN];
} user_info_t;

// Initialize user subsystem
void user_init(void);

// Get current user information
int get_current_user(user_info_t *user);
