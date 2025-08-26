#ifndef ARM_TYPES_H
#define ARM_TYPES_H

// Tipos básicos para ARM-32 - VERSIÓN MODULAR
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long long int64_t;

// Tipos de punteros
typedef uint32_t uintptr_t;
typedef int32_t intptr_t;

// Tipo booleano
typedef enum {
    false = 0,
    true = 1
} bool;

// Constantes
#define NULL ((void*)0)

#endif // ARM_TYPES_H
