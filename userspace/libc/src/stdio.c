// stdio.c - Basic I/O functions for IR0 libc
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ir0/syscall.h>

#define BUFSIZ 1024

// Standard streams (initialized at runtime)
static FILE _stdin = {STDIN_FILENO, NULL, 0, 0, 0, 0, 0, 0};
static FILE _stdout = {STDOUT_FILENO, NULL, 0, 0, 0, 0, 0, 1};
static FILE _stderr = {STDERR_FILENO, NULL, 0, 0, 0, 0, 0, 1};

FILE *stdin = &_stdin;
FILE *stdout = &_stdout;
FILE *stderr = &_stderr;

// Simple putchar implementation
int putchar(int c)
{
    char ch = (char)c;
    return write(STDOUT_FILENO, &ch, 1) == 1 ? c : -1;
}

int getchar(void)
{
    char ch;
    return read(STDIN_FILENO, &ch, 1) == 1 ? (unsigned char)ch : EOF;
}

// Simple puts implementation
int puts(const char *str)
{
    if (!str)
        return -1;

    const char *p = str;
    while (*p)
    {
        if (putchar(*p) == -1)
            return -1;
        p++;
    }

    return putchar('\n');
}

// Helper function for formatting
static int format_output(char *buf, size_t buf_size, const char *format, va_list args)
{
    size_t pos = 0;
    const char *p = format;

    while (*p && pos < buf_size - 1)
    {
        if (*p == '%' && *(p + 1))
        {
            p++;
            switch (*p)
            {
            case 'd':
            case 'i':
            {
                int num = va_arg(args, int);
                char digits[12];
                int i = 0;
                int negative = 0;

                if (num == 0)
                {
                    if (pos < buf_size - 1) buf[pos++] = '0';
                }
                else
                {
                    if (num < 0)
                    {
                        negative = 1;
                        num = -num;
                    }

                    while (num > 0 && i < 11)
                    {
                        digits[i++] = '0' + (num % 10);
                        num /= 10;
                    }

                    if (negative && pos < buf_size - 1)
                        buf[pos++] = '-';

                    while (i > 0 && pos < buf_size - 1)
                        buf[pos++] = digits[--i];
                }
                break;
            }
            case 'u':
            {
                unsigned int num = va_arg(args, unsigned int);
                char digits[12];
                int i = 0;

                if (num == 0)
                {
                    if (pos < buf_size - 1) buf[pos++] = '0';
                }
                else
                {
                    while (num > 0 && i < 11)
                    {
                        digits[i++] = '0' + (num % 10);
                        num /= 10;
                    }
                    while (i > 0 && pos < buf_size - 1)
                        buf[pos++] = digits[--i];
                }
                break;
            }
            case 'x':
            case 'X':
            {
                unsigned int num = va_arg(args, unsigned int);
                char digits[16];
                int i = 0;
                const char *hex = (*p == 'x') ? "0123456789abcdef" : "0123456789ABCDEF";

                if (num == 0)
                {
                    if (pos < buf_size - 1) buf[pos++] = '0';
                }
                else
                {
                    while (num > 0 && i < 15)
                    {
                        digits[i++] = hex[num % 16];
                        num /= 16;
                    }
                    while (i > 0 && pos < buf_size - 1)
                        buf[pos++] = digits[--i];
                }
                break;
            }
            case 's':
            {
                const char *str = va_arg(args, const char *);
                if (str)
                {
                    while (*str && pos < buf_size - 1)
                        buf[pos++] = *str++;
                }
                break;
            }
            case 'c':
            {
                int ch = va_arg(args, int);
                if (pos < buf_size - 1)
                    buf[pos++] = (char)ch;
                break;
            }
            case '%':
            {
                if (pos < buf_size - 1)
                    buf[pos++] = '%';
                break;
            }
            default:
                if (pos < buf_size - 2)
                {
                    buf[pos++] = '%';
                    buf[pos++] = *p;
                }
                break;
            }
        }
        else
        {
            buf[pos++] = *p;
        }
        p++;
    }

    buf[pos] = '\0';
    return pos;
}

int printf(const char *format, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, format);
    int len = format_output(buf, sizeof(buf), format, args);
    va_end(args);
    
    write(STDOUT_FILENO, buf, len);
    return len;
}

int fprintf(FILE *stream, const char *format, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, format);
    int len = format_output(buf, sizeof(buf), format, args);
    va_end(args);
    
    if (stream)
        return fwrite(buf, 1, len, stream);
    return -1;
}

int sprintf(char *str, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int len = format_output(str, 4096, format, args);
    va_end(args);
    return len;
}

int snprintf(char *str, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int len = format_output(str, size, format, args);
    va_end(args);
    return len;
}

// FILE operations
FILE *fopen(const char *pathname, const char *mode)
{
    int flags = 0;
    int fd;

    if (!pathname || !mode)
        return NULL;

    // Parse mode
    if (mode[0] == 'r')
        flags = 0; // O_RDONLY
    else if (mode[0] == 'w')
        flags = 1 | 0x200; // O_WRONLY | O_CREAT
    else if (mode[0] == 'a')
        flags = 1 | 0x200 | 0x400; // O_WRONLY | O_CREAT | O_APPEND
    else
        return NULL;

    fd = (int)ir0_open(pathname, flags, 0644);
    if (fd < 0)
        return NULL;

    FILE *f = malloc(sizeof(FILE));
    if (!f)
    {
        close(fd);
        return NULL;
    }

    f->fd = fd;
    f->buffer = malloc(BUFSIZ);
    f->buffer_size = BUFSIZ;
    f->buffer_pos = 0;
    f->buffer_len = 0;
    f->eof = 0;
    f->error = 0;
    f->mode = (mode[0] == 'r') ? 0 : 1;

    return f;
}

int fclose(FILE *stream)
{
    if (!stream)
        return EOF;

    fflush(stream);
    
    if (stream->buffer)
        free(stream->buffer);
    
    int ret = close(stream->fd);
    free(stream);
    
    return ret == 0 ? 0 : EOF;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    if (!stream || !ptr || size == 0 || nmemb == 0)
        return 0;

    size_t total = size * nmemb;
    size_t read_count = 0;
    char *dest = ptr;

    while (read_count < total)
    {
        if (stream->buffer_pos >= stream->buffer_len)
        {
            // Refill buffer
            ssize_t n = read(stream->fd, stream->buffer, stream->buffer_size);
            if (n <= 0)
            {
                stream->eof = (n == 0);
                stream->error = (n < 0);
                break;
            }
            stream->buffer_len = n;
            stream->buffer_pos = 0;
        }

        size_t available = stream->buffer_len - stream->buffer_pos;
        size_t to_copy = (total - read_count < available) ? (total - read_count) : available;
        
        memcpy(dest + read_count, stream->buffer + stream->buffer_pos, to_copy);
        stream->buffer_pos += to_copy;
        read_count += to_copy;
    }

    return read_count / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    if (!stream || !ptr || size == 0 || nmemb == 0)
        return 0;

    size_t total = size * nmemb;
    ssize_t written = write(stream->fd, ptr, total);
    
    if (written < 0)
    {
        stream->error = 1;
        return 0;
    }

    return written / size;
}

int fseek(FILE *stream, long offset, int whence)
{
    if (!stream)
        return -1;

    fflush(stream);
    stream->buffer_pos = 0;
    stream->buffer_len = 0;
    stream->eof = 0;

    long result = (long)syscall3(SYS_LSEEK, stream->fd, offset, whence);
    return (result < 0) ? -1 : 0;
}

long ftell(FILE *stream)
{
    if (!stream)
        return -1;

    return (long)syscall3(SYS_LSEEK, stream->fd, 0, SEEK_CUR);
}

void rewind(FILE *stream)
{
    if (stream)
    {
        fseek(stream, 0, SEEK_SET);
        stream->eof = 0;
        stream->error = 0;
    }
}

int feof(FILE *stream)
{
    return stream ? stream->eof : 0;
}

int ferror(FILE *stream)
{
    return stream ? stream->error : 0;
}

int fflush(FILE *stream)
{
    // For now, no-op since we write directly
    (void)stream;
    return 0;
}

int fgetc(FILE *stream)
{
    unsigned char c;
    return (fread(&c, 1, 1, stream) == 1) ? c : EOF;
}

int fputc(int c, FILE *stream)
{
    unsigned char ch = c;
    return (fwrite(&ch, 1, 1, stream) == 1) ? c : EOF;
}

char *fgets(char *s, int size, FILE *stream)
{
    if (!s || size <= 0 || !stream)
        return NULL;

    int i = 0;
    while (i < size - 1)
    {
        int c = fgetc(stream);
        if (c == EOF)
        {
            if (i == 0)
                return NULL;
            break;
        }
        s[i++] = c;
        if (c == '\n')
            break;
    }
    s[i] = '\0';
    return s;
}

int fputs(const char *s, FILE *stream)
{
    if (!s || !stream)
        return EOF;

    size_t len = strlen(s);
    return (fwrite(s, 1, len, stream) == len) ? 0 : EOF;
}