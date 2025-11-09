#pragma once


// Niveles de panic semánticos
typedef enum
{
    PANIC_KERNEL_BUG = 0, // Bug en kernel code
    PANIC_HARDWARE_FAULT, // Hardware malfunction
    PANIC_OUT_OF_MEMORY,  // System out of memory
    PANIC_STACK_OVERFLOW, // Stack corruption
    PANIC_ASSERT_FAILED   // Assertion failure

} panic_level_t;


/*
 * ============ Panic() ================
 * Detiene el kernel completamente y muestra mensaje de error crítico.
 * Limpia pantalla, muestra mensaje en rojo, deshabilita interrupciones
 * y entra en loop infinito con hlt para mantener la cpu en modo de ahorro.
 * 
 * Cómo es un estado límite del sistema, la idea es frenar en seco la ejecución del sistema. 
 */




void panic(const char *message);
void cpu_relax();
void panic(const char* message);
void panic_advanced(const char *message, panic_level_t level, const char *file, int line);
void dump_registers(void);      
void dump_stack_trace(void);    
void dump_memory_info(void); 

// Macros para facilidad en el panic event
#define BUG_ON(condition) \
    do { \
        if (unlikely(condition)) \
        { \
            panic_advanced("BUG_ON: " #condition, PANIC_KERNEL_BUG, __FILE__, __LINE__); \
        } \
    } while(0)

#define ASSERT(condition) \
    do { \
        if (unlikely(!(condition))) \
        { \
            panic_advanced("ASSERT failed: " #condition, PANIC_ASSERT_FAILED, __FILE__, __LINE__); \
        } \
    } while(0)
      
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

