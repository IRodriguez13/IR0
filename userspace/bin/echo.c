#include <ir0/syscall.h>

// Minimal strlen implementation
static int strlen(const char *str) {
    int len = 0;
    while (str[len]) len++;
    return len;
}

void _start(int argc, char *argv[]) {
    // Note: Arguments might not be passed correctly yet depending on kernel implementation.
    // However, we follow the standard ABI convention for _start.
    
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (i > 1) {
                ir0_write(1, " ", 1);
            }
            ir0_write(1, argv[i], strlen(argv[i]));
        }
    } else {
        // Default message if no args
        const char *msg = "echo: no arguments provided";
        ir0_write(1, msg, strlen(msg));
    }
    
    ir0_write(1, "\n", 1);
    ir0_exit(0);
}
