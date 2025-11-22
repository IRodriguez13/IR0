#ifndef STRING_H
#define STRING_H

#include <stddef.h>
#include <stdint.h>
#include "stdarg.h"

// ===============================================================================
// STRING FUNCTIONS
// ===============================================================================

// Basic string functions
size_t strlen(const char *str);

// Kernel-specific string functions
size_t kstrlen(const char *s);
char *kstrncpy(char *dest, const char *src, size_t n);
int kstrcmp(const char *s1, const char *s2);
int kstrncmp(const char *s1, const char *s2, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);

// String search functions
char *strchr(const char *str, int c);
char *strrchr(const char *str, int c);
char *strstr(const char *haystack, const char *needle);

// Memory functions
void *memset(void *ptr, int value, size_t num);
void *memcpy(void *dest, const void *src, size_t num);
void *memmove(void *dest, const void *src, size_t num);
int memcmp(const void *ptr1, const void *ptr2, size_t num);

// Kernel-specific memory functions
void *kmemset(void *s, int c, size_t n);
void *kmemcpy(void *dest, const void *src, size_t n);

// String tokenization
char *strtok(char *str, const char *delim);
char *strtok_r(char *str, const char *delim, char **saveptr);

// String conversion functions
int atoi(const char *str);
long atol(const char *str);
unsigned long strtoul(const char *str, char **endptr, int base);
long strtol(const char *str, char **endptr, int base);
char *itoa(int value, char *str, int base);

// String formatting
int snprintf(char *str, size_t size, const char *format, ...);
int sprintf(char *str, const char *format, ...);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);

// Character classification
int isdigit(int c);
int isalpha(int c);
int isalnum(int c);
int isspace(int c);
int isupper(int c);
int islower(int c);

// Character conversion
int toupper(int c);
int tolower(int c);

#endif // STRING_H
