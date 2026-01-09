#include "permissions.h"
#include "process.h"
#include <fs/vfs.h>
#include <fs/minix_fs.h>
#include <ir0/stat.h>
#include <string.h>

/* Initialize simple user system */
void init_simple_users(void)
{
    /* Nothing to initialize - everything is hardcoded */
}

/* Get current process UID */
uint32_t get_current_uid(void)
{
    return current_process ? current_process->uid : ROOT_UID;
}

/* Get current process GID */
uint32_t get_current_gid(void)
{
    return current_process ? current_process->gid : ROOT_GID;
}

/* Check if process is root */
bool is_root(const struct process *process)
{
    return process && process->uid == ROOT_UID;
}

/* Check file access permissions - Unix style */
bool check_file_access(const char *path, int mode, const struct process *process)
{
    if (!path || !process)
        return false;

    /* Root can do everything */
    if (process->uid == ROOT_UID)
        return true;

    /* Get file stats */
    stat_t st;
    if (vfs_stat(path, &st) != 0)
        return false;

    uint16_t file_mode = st.st_mode;
    uint32_t file_uid = st.st_uid;
    uint32_t file_gid = st.st_gid;

    /* Check owner permissions */
    if (process->uid == file_uid) {
        if ((mode & ACCESS_READ) && !(file_mode & S_IRUSR))
            return false;
        if ((mode & ACCESS_WRITE) && !(file_mode & S_IWUSR))
            return false;
        if ((mode & ACCESS_EXEC) && !(file_mode & S_IXUSR))
            return false;
        return true;
    }

    /* Check group permissions */
    if (process->gid == file_gid) {
        if ((mode & ACCESS_READ) && !(file_mode & S_IRGRP))
            return false;
        if ((mode & ACCESS_WRITE) && !(file_mode & S_IWGRP))
            return false;
        if ((mode & ACCESS_EXEC) && !(file_mode & S_IXGRP))
            return false;
        return true;
    }

    /* Check other permissions */
    if ((mode & ACCESS_READ) && !(file_mode & S_IROTH))
        return false;
    if ((mode & ACCESS_WRITE) && !(file_mode & S_IWOTH))
        return false;
    if ((mode & ACCESS_EXEC) && !(file_mode & S_IXOTH))
        return false;

    return true;
}
