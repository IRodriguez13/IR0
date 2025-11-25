// atoi.c - String to integer conversion
#include <stdlib.h>
#include <ctype.h>

int atoi(const char *str) {
    int result = 0;
    int sign = 1;

    // Skip whitespace
    while (isspace(*str)) {
        str++;
    }

    // Check sign
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }

    // Convert digits
    while (isdigit(*str)) {
        result = result * 10 + (*str - '0');
        str++;
    }

    return sign * result;
}

long atol(const char *str) {
    long result = 0;
    int sign = 1;

    while (isspace(*str)) {
        str++;
    }

    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }

    while (isdigit(*str)) {
        result = result * 10 + (*str - '0');
        str++;
    }

    return sign * result;
}
