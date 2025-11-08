#ifndef _IR0_PATH_H
#define _IR0_PATH_H

#include <stddef.h>

// Normalize a path, handling ".." and "." components
// Returns the normalized path in dest
// dest must be at least as large as src
// Returns 0 on success, -1 on error
int normalize_path(const char *src, char *dest, size_t size);

// Join two paths together, handling ".." and "." components
// Returns the joined path in dest
// dest must be large enough to hold both paths plus a separator
// Returns 0 on success, -1 on error
int join_paths(const char *base, const char *rel, char *dest, size_t size);

// Test if a path is absolute
int is_absolute_path(const char *path);

// Get parent directory path
// Returns the parent directory path in dest
// Returns 0 on success, -1 on error
int get_parent_path(const char *path, char *dest, size_t size);

#endif /* _IR0_PATH_H */