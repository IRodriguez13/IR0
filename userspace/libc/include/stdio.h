#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

// File descriptors
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

// FILE structure
typedef struct {
    int fd;
    char *buffer;
    size_t buffer_size;
    size_t buffer_pos;
    size_t buffer_len;
    int eof;
    int error;
    int mode; // 0=read, 1=write, 2=read/write
} FILE;

// Standard streams
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

// Seek constants
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// File modes
#define EOF (-1)

// Basic I/O functions
int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int sprintf(char *str, const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);
int puts(const char *str);
int putchar(int c);
int getchar(void);

// File operations
FILE *fopen(const char *pathname, const char *mode);
int fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
int fflush(FILE *stream);

// Character I/O
int fgetc(FILE *stream);
int fputc(int c, FILE *stream);
char *fgets(char *s, int size, FILE *stream);
int fputs(const char *s, FILE *stream);

// Low-level file operations (from unistd.h but declared here for compatibility)
int open(const char *pathname, int flags);
int close(int fd);
int read(int fd, void *buf, size_t count);
int write(int fd, const void *buf, size_t count);

