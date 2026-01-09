#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <ir0/types.h>

/* Forward declaration */
struct process;

/* Simple user system - hardcoded */
#define ROOT_UID     0
#define ROOT_GID     0
#define USER_UID     1000
#define USER_GID     1000

/* Default umask (022 = ----w--w-) */
#define DEFAULT_UMASK 0022

/* Access modes for check_file_access() */
#define ACCESS_READ    1
#define ACCESS_WRITE   2
#define ACCESS_EXEC    4

/* Function declarations */
bool check_file_access(const char *path, int mode, const struct process *process);
uint32_t get_current_uid(void);
uint32_t get_current_gid(void);
bool is_root(const struct process *process);
void init_simple_users(void);
