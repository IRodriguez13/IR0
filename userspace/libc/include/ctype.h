#pragma once

static inline int isdigit(int c) {
    return c >= '0' && c <= '9';
}

static inline int isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

static inline int isalpha(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static inline int isupper(int c) {
    return c >= 'A' && c <= 'Z';
}

static inline int islower(int c) {
    return c >= 'a' && c <= 'z';
}

static inline int toupper(int c) {
    if (islower(c))
        return c - 'a' + 'A';
    return c;
}

static inline int tolower(int c) {
    if (isupper(c))
        return c - 'A' + 'a';
    return c;
}
