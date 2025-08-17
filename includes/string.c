/*
Son las implementaciones de las funciones mágicas que usás cuando hacés tus To Do Lists. (todo bien con las To Do)

*/

#include "string.h"

void *memset(void *dest, int val, size_t count)
{
    unsigned char *ptr = dest;
    while (count--)
        *ptr++ = (unsigned char)val;
    return dest;
}

void *memcpy(void *dest, const void *src, size_t count)
{
    const unsigned char *s = src;
    unsigned char *d = dest;
    while (count--)
        *d++ = *s++;
    return dest;
}

int memcmp(const void *a, const void *b, size_t count)
{
    const unsigned char *p1 = a, *p2 = b;
    while (count--)
        if (*p1++ != *p2++)
            return *(p1 - 1) - *(p2 - 1);
    return 0;
}

size_t strlen(const char *str)
{
    size_t len = 0;
    while (*str++)
        len++;
    return len;
}

int strcmp(const char *str1, const char *str2)
{
    while (*str1 && (*str1 == *str2))
    {
        str1++;
        str2++;
    }
    return *(const unsigned char *)str1 - *(const unsigned char *)str2;
}

char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while (*src)
        *d++ = *src++;
    *d = '\0';
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n)
{
    char *d = dest;
    while (n-- && *src)
        *d++ = *src++;
    while (n--)
        *d++ = '\0';
    return dest;
}

char *strrchr(const char *str, int c)
{
    char *last = NULL;
    while (*str)
    {
        if (*str == (char)c)
            last = (char *)str;
        str++;
    }
    if (c == '\0')
        return (char *)str;
    return last;
}
