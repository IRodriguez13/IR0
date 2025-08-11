#pragma once
/*
 * Detección automática de arquitectura en tiempo de compilación
 * Usado por el sistema de build. Esto me debería servir en el sistema de "estrategias de compilación" y además permite que el kernel escale 
 * a más arquitecturas sin problema.
 */

#if defined(__x86_64__) || defined(__amd64__)
    
    #define ARCH_X86_64
    #define ARCH_NAME "x86-64"
    #define ARCH_WORD_SIZE 64

#elif defined(__i386__) || defined(__i486__) || defined(__i586__) || defined(__i686__)
    
    #define ARCH_X86_32
    #define ARCH_NAME "x86-32"
    #define ARCH_WORD_SIZE 32

#elif defined(__aarch64__)

    #define ARCH_ARM64
    #define ARCH_NAME "ARM64"
    #define ARCH_WORD_SIZE 64

#elif defined(__arm__)
  
    #define ARCH_ARM32
    #define ARCH_NAME "ARM32"
    #define ARCH_WORD_SIZE 32

#else
    #error "Arquitectura no soportada"

#endif

// Por las dudas
#define IS_64_BIT (ARCH_WORD_SIZE == 64)
#define IS_32_BIT (ARCH_WORD_SIZE == 32)

