// strtod.c - String to double conversion
#include <stdlib.h>
#include <ctype.h>

double strtod(const char *nptr, char **endptr) {
    double result = 0.0;
    int sign = 1;
    const char *p = nptr;

    // Skip whitespace
    while (isspace(*p)) {
        p++;
    }

    // Check sign
    if (*p == '-') {
        sign = -1;
        p++;
    } else if (*p == '+') {
        p++;
    }

    // Integer part
    while (isdigit(*p)) {
        result = result * 10.0 + (*p - '0');
        p++;
    }

    // Fraction part
    if (*p == '.') {
        double fraction = 0.1;
        p++;
        while (isdigit(*p)) {
            result += (*p - '0') * fraction;
            fraction *= 0.1;
            p++;
        }
    }

    // Exponent part (optional, basic implementation)
    if (*p == 'e' || *p == 'E') {
        p++;
        int exp_sign = 1;
        int exp_val = 0;

        if (*p == '-') {
            exp_sign = -1;
            p++;
        } else if (*p == '+') {
            p++;
        }

        while (isdigit(*p)) {
            exp_val = exp_val * 10 + (*p - '0');
            p++;
        }

        double power = 1.0;
        for (int i = 0; i < exp_val; i++) {
            if (exp_sign == 1)
                power *= 10.0;
            else
                power /= 10.0;
        }
        result *= power;
    }

    if (endptr) {
        *endptr = (char *)p;
    }

    return sign * result;
}
