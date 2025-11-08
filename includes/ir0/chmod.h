#ifndef _CHMOD_H
#define _CHMOD_H

#include <stdint.h>
#include <ir0/types.h>  // For mode_t

// File permission bits
#define S_ISUID 0004000    /* Set user ID on execution */
#define S_ISGID 0002000    /* Set group ID on execution */
#define S_ISVTX 0001000    /* Save swapped text after use (sticky) */

#define S_IRWXU 0000700    /* RWX mask for owner */
#define S_IRUSR 0000400    /* R for owner */
#define S_IWUSR 0000200    /* W for owner */
#define S_IXUSR 0000100    /* X for owner */

#define S_IRWXG 0000070    /* RWX mask for group */
#define S_IRGRP 0000040    /* R for group */
#define S_IWGRP 0000020    /* W for group */
#define S_IXGRP 0000010    /* X for group */

#define S_IRWXO 0000007    /* RWX mask for other */
#define S_IROTH 0000004    /* R for other */
#define S_IWOTH 0000002    /* W for other */
#define S_IXOTH 0000001    /* X for other */

// Function declarations
int chmod(const char *path, mode_t mode);
int parse_mode(const char *mode_str);

#endif