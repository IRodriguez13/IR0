#ifndef _FS_TYPES_H
#define _FS_TYPES_H

#include <stdint.h>

// Filesystem type identification
const char* get_fs_type(uint8_t system_id);

#endif /* _FS_TYPES_H */