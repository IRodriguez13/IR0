// stdio.c - Basic I/O functions for IR0 libc
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>

// Simple putchar implementation
int putchar(int c)
{
    char ch = (char)c;
    return write(STDOUT_FILENO, &ch, 1) == 1 ? c : -1;
}

// Simple puts implementation
int puts(const char *str)
{
    if (!str)
        return -1;

    // Write string
    const char *p = str;
    while (*p)
    {
        if (putchar(*p) == -1)
            return -1;
        p++;
    }

    // Write newline
    return putchar('\n');
}

// Simple printf implementation (very basic)
int printf(const char *format, ...)
{
    if (!format)
        return -1;

    va_list args;
    va_start(args, format);

    int count = 0;
    const char *p = format;

    while (*p)
    {
        if (*p == '%' && *(p + 1))
        {
            p++; // Skip %
            switch (*p)
            {
            case 'd':
            {
                int num = va_arg(args, int);
                // Simple integer to string conversion
                if (num == 0)
                {
                    putchar('0');
                    count++;
                }
                else
                {
                    char digits[12];
                    int i = 0;
                    int negative = 0;

                    if (num < 0)
                    {
                        negative = 1;
                        num = -num;
                    }

                    while (num > 0)
                    {
                        digits[i++] = '0' + (num % 10);
                        num /= 10;
                    }

                    if (negative)
                    {
                        putchar('-');
                        count++;
                    }

                    while (i > 0)
                    {
                        putchar(digits[--i]);
                        count++;
                    }
                }
                break;
            }
            case 's':
            {
                const char *str = va_arg(args, const char *);
                if (str)
                {
                    while (*str)
                    {
                        putchar(*str++);
                        count++;
                    }
                }
                break;
            }
            case 'c':
            {
                int ch = va_arg(args, int);
                putchar(ch);
                count++;
                break;
            }
            case '%':
            {
                putchar('%');
                count++;
                break;
            }
            default:
                putchar('%');
                putchar(*p);
                count += 2;
                break;
            }
        }
        else
        {
            putchar(*p);
            count++;
        }
        p++;
    }

    va_end(args);
    return count;
}