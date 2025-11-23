// echo.c - Echo command for IR0 (like Linux echo)
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    // Skip program name (argv[0])
    for (int i = 1; i < argc; i++) {
        if (i > 1) {
            putchar(' '); // Space between arguments
        }
        printf("%s", argv[i]);
    }
    
    putchar('\n'); // Newline at end
    return 0;
}
