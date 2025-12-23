#include <ir0/user.h>
#include <string.h>

/* For now, we'll have a single root user */
static user_info_t root_user = {
    .uid = 0,
    .name = "root",
    .home = "/root",
    .shell = "/bin/sh"
};

/* Current user info */
static user_info_t *current_user = NULL;

void user_init(void) {
    /* Initialize with root user */
    current_user = &root_user;
}

int get_current_user(user_info_t *user) {
    if (!user || !current_user)
        return -1;
        
    /* Copy current user info */
    user->uid = current_user->uid;
    strncpy(user->name, current_user->name, sizeof(user->name) - 1);
    user->name[sizeof(user->name) - 1] = '\0';
    
    strncpy(user->home, current_user->home, sizeof(user->home) - 1);
    user->home[sizeof(user->home) - 1] = '\0';
    
    strncpy(user->shell, current_user->shell, sizeof(user->shell) - 1);
    user->shell[sizeof(user->shell) - 1] = '\0';
    
    return 0;
}