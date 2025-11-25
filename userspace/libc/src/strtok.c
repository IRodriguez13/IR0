// strtok.c - String tokenization
#include <string.h>

char *strtok(char *str, const char *delim)
{
    static char *last_str = NULL;
    char *token;

    if (str == NULL)
        str = last_str;

    if (str == NULL)
        return NULL;

    // Skip leading delimiters
    while (*str)
    {
        const char *d = delim;
        int is_delim = 0;
        while (*d)
        {
            if (*str == *d)
            {
                is_delim = 1;
                break;
            }
            d++;
        }
        if (!is_delim)
            break;
        str++;
    }

    if (*str == '\0')
    {
        last_str = NULL;
        return NULL;
    }

    token = str;

    // Find end of token
    while (*str)
    {
        const char *d = delim;
        int is_delim = 0;
        while (*d)
        {
            if (*str == *d)
            {
                is_delim = 1;
                break;
            }
            d++;
        }
        if (is_delim)
        {
            *str = '\0';
            last_str = str + 1;
            return token;
        }
        str++;
    }

    last_str = NULL;
    return token;
}
