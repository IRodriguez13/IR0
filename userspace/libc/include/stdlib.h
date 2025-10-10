#pragma once

#include <stddef.h>

// Memory management
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

// Process control
void exit(int status);
int system(const char *command);

// String conversion
int atoi(const char *str);
long atol(const char *str);

