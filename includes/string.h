#ifndef _STRING_H
#define _STRING_H

#include "stddef.h"

void *memset(void *dest, int val, size_t count);
void *memcpy(void *dest, const void *src, size_t count);
int memcmp(const void *a, const void *b, size_t count);
size_t strlen(const char *str);
int strcmp(const char *str1, const char *str2);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strrchr(const char *str, int c);

#endif
