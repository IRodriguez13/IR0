#pragma once

#include <ir0/print.h>
#include <ir0/stdbool.h>
#include <stdint.h>

// Login system configuration
typedef struct {
    const char *correct_password;
    int max_attempts;
    bool case_sensitive;
} login_config_t;

// Login system state
typedef struct {
    int attempts;
    bool authenticated;
    bool locked;
} login_state_t;

// Function declarations
int login_init(const login_config_t *config);
int login_authenticate(void);
void login_reset(void);
bool login_is_authenticated(void);
void login_lock_system(void);
