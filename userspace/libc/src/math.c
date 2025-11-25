// math.c - Mathematical functions for IR0 libc
#include <math.h>

// Helper: absolute value for double
double fabs(double x) {
    return x < 0 ? -x : x;
}

float fabsf(float x) {
    return x < 0 ? -x : x;
}

// Floor and ceiling
double floor(double x) {
    long i = (long)x;
    return (x < i) ? (double)(i - 1) : (double)i;
}

float floorf(float x) {
    return (float)floor(x);
}

double ceil(double x) {
    long i = (long)x;
    return (x > i) ? (double)(i + 1) : (double)i;
}

float ceilf(float x) {
    return (float)ceil(x);
}

// Modulo
double fmod(double x, double y) {
    if (y == 0.0) return NAN;
    return x - floor(x / y) * y;
}

double modf(double x, double *iptr) {
    *iptr = floor(x);
    return x - *iptr;
}

// Square root using Newton-Raphson
double sqrt(double x) {
    if (x < 0) return NAN;
    if (x == 0) return 0;
    
    double guess = x;
    double epsilon = 1e-10;
    
    for (int i = 0; i < 50; i++) {
        double next = (guess + x / guess) / 2.0;
        if (fabs(next - guess) < epsilon)
            break;
        guess = next;
    }
    
    return guess;
}

float sqrtf(float x) {
    return (float)sqrt(x);
}

// Cube root
double cbrt(double x) {
    if (x == 0) return 0;
    
    int negative = x < 0;
    if (negative) x = -x;
    
    double guess = x;
    for (int i = 0; i < 20; i++) {
        guess = (2.0 * guess + x / (guess * guess)) / 3.0;
    }
    
    return negative ? -guess : guess;
}

// Power function
double pow(double x, double y) {
    if (y == 0.0) return 1.0;
    if (x == 0.0) return 0.0;
    if (x == 1.0) return 1.0;
    
    // Handle integer exponents efficiently
    if (y == (long)y) {
        long n = (long)y;
        double result = 1.0;
        int negative = n < 0;
        if (negative) n = -n;
        
        double base = x;
        while (n > 0) {
            if (n & 1) result *= base;
            base *= base;
            n >>= 1;
        }
        
        return negative ? 1.0 / result : result;
    }
    
    // For non-integer: x^y = exp(y * log(x))
    return exp(y * log(x));
}

float powf(float x, float y) {
    return (float)pow(x, y);
}

// Natural exponential using Taylor series
double exp(double x) {
    if (x == 0) return 1.0;
    
    // Reduce range using exp(x) = exp(x/2)^2
    int reduce = 0;
    while (fabs(x) > 1.0) {
        x /= 2.0;
        reduce++;
    }
    
    // Taylor series: exp(x) = 1 + x + x^2/2! + x^3/3! + ...
    double result = 1.0;
    double term = 1.0;
    
    for (int i = 1; i < 30; i++) {
        term *= x / i;
        result += term;
        if (fabs(term) < 1e-15) break;
    }
    
    // Square result 'reduce' times
    for (int i = 0; i < reduce; i++) {
        result *= result;
    }
    
    return result;
}

// Natural logarithm using series expansion
double log(double x) {
    if (x <= 0) return NAN;
    if (x == 1.0) return 0.0;
    
    // Use log(x) = log(m * 2^e) = log(m) + e*log(2)
    // where 0.5 <= m < 1.0
    int exp_val = 0;
    while (x >= 2.0) {
        x /= 2.0;
        exp_val++;
    }
    while (x < 0.5) {
        x *= 2.0;
        exp_val--;
    }
    
    // Now compute log(x) for x in [0.5, 2.0] using series
    // log(x) = log((1+y)/(1-y)) = 2(y + y^3/3 + y^5/5 + ...)
    // where y = (x-1)/(x+1)
    double y = (x - 1.0) / (x + 1.0);
    double y2 = y * y;
    double result = 0.0;
    double term = y;
    
    for (int i = 1; i < 30; i += 2) {
        result += term / i;
        term *= y2;
        if (fabs(term / i) < 1e-15) break;
    }
    
    result *= 2.0;
    result += exp_val * M_LN2;
    
    return result;
}

double log10(double x) {
    return log(x) / M_LN10;
}

double log2(double x) {
    return log(x) / M_LN2;
}

// Sine using Taylor series
double sin(double x) {
    // Reduce to [-pi, pi]
    x = fmod(x, 2.0 * M_PI);
    if (x > M_PI) x -= 2.0 * M_PI;
    if (x < -M_PI) x += 2.0 * M_PI;
    
    // Taylor series: sin(x) = x - x^3/3! + x^5/5! - x^7/7! + ...
    double result = 0.0;
    double term = x;
    double x2 = x * x;
    
    for (int i = 1; i < 20; i += 2) {
        result += term;
        term *= -x2 / ((i + 1) * (i + 2));
    }
    
    return result;
}

float sinf(float x) {
    return (float)sin(x);
}

// Cosine using Taylor series
double cos(double x) {
    // Reduce to [-pi, pi]
    x = fmod(x, 2.0 * M_PI);
    if (x > M_PI) x -= 2.0 * M_PI;
    if (x < -M_PI) x += 2.0 * M_PI;
    
    // Taylor series: cos(x) = 1 - x^2/2! + x^4/4! - x^6/6! + ...
    double result = 0.0;
    double term = 1.0;
    double x2 = x * x;
    
    for (int i = 0; i < 20; i += 2) {
        result += term;
        term *= -x2 / ((i + 1) * (i + 2));
    }
    
    return result;
}

float cosf(float x) {
    return (float)cos(x);
}

double tan(double x) {
    double c = cos(x);
    if (c == 0.0) return INFINITY;
    return sin(x) / c;
}

float tanf(float x) {
    return (float)tan(x);
}

// Arcsine using series
double asin(double x) {
    if (x < -1.0 || x > 1.0) return NAN;
    if (x == -1.0) return -M_PI_2;
    if (x == 1.0) return M_PI_2;
    
    // Use asin(x) = x + x^3/6 + 3x^5/40 + ... for |x| < 0.5
    // Otherwise use asin(x) = pi/2 - asin(sqrt(1-x^2))
    if (fabs(x) > 0.5) {
        double sign = x < 0 ? -1.0 : 1.0;
        return sign * (M_PI_2 - asin(sqrt(1.0 - x * x)));
    }
    
    double result = x;
    double term = x;
    double x2 = x * x;
    
    for (int n = 1; n < 20; n++) {
        term *= x2 * (2 * n - 1) * (2 * n - 1) / ((2 * n) * (2 * n + 1));
        result += term;
        if (fabs(term) < 1e-15) break;
    }
    
    return result;
}

double acos(double x) {
    return M_PI_2 - asin(x);
}

double atan(double x) {
    if (x > 1.0) return M_PI_2 - atan(1.0 / x);
    if (x < -1.0) return -M_PI_2 - atan(1.0 / x);
    
    // Series: atan(x) = x - x^3/3 + x^5/5 - x^7/7 + ...
    double result = 0.0;
    double term = x;
    double x2 = x * x;
    
    for (int i = 1; i < 30; i += 2) {
        result += term / i;
        term *= -x2;
        if (fabs(term / i) < 1e-15) break;
    }
    
    return result;
}

double atan2(double y, double x) {
    if (x > 0) return atan(y / x);
    if (x < 0 && y >= 0) return atan(y / x) + M_PI;
    if (x < 0 && y < 0) return atan(y / x) - M_PI;
    if (x == 0 && y > 0) return M_PI_2;
    if (x == 0 && y < 0) return -M_PI_2;
    return 0; // x == 0 && y == 0
}

// Hyperbolic functions
double sinh(double x) {
    return (exp(x) - exp(-x)) / 2.0;
}

double cosh(double x) {
    return (exp(x) + exp(-x)) / 2.0;
}

double tanh(double x) {
    double e2x = exp(2.0 * x);
    return (e2x - 1.0) / (e2x + 1.0);
}
