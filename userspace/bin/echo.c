// echo.c - Echo command for IR0 userspace
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    // Skip program name (argv[0])
    for (int i = 1; i < argc; i++)
    {
        if (i > 1)
        {
            putchar(' '); // Space between arguments
        }
        printf("%s", argv[i]);
    }

    putchar('\n'); // Newline at end
    return 0;
}

// Alternative simple version without argc/argv
void _start(void)
{
    // For now, just print a test message
    printf("Hello from /bin/echo!\n");
    exit(0);
}