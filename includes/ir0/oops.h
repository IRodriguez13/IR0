#pragma once


// Niveles de panic semánticos
typedef enum
{
    PANIC_KERNEL_BUG = 0, // Bug en kernel code
    PANIC_HARDWARE_FAULT = 1, // Hardware malfunction
    PANIC_OUT_OF_MEMORY = 2,  // System out of memory
    PANIC_STACK_OVERFLOW = 3, // Stack corruption
    PANIC_ASSERT_FAILED= 4,   // Assertion failure
    PANIC_MEM = 5,            // Memory operation error (null ptr, invalid access, etc.)
    TESTING = 6,
    RUNNING_OUT_PROCESS = 7
} panic_level_t;


/*
 * ============ Panic() ================
 * Detiene el kernel completamente y muestra mensaje de error crítico.
 * Limpia pantalla, muestra mensaje en rojo, deshabilita interrupciones
 * y entra en loop infinito con hlt para mantener la cpu en modo de ahorro.
 * 
 * Cómo es un estado límite del sistema, la idea es frenar en seco la ejecución del sistema. 
 */





#ifdef __cplusplus
extern "C" {
#endif

void panic(const char *message);
void panicex(const char *message, panic_level_t level, const char *file, int line, const char *caller); /*Panic with more detailed logs*/
void cpu_relax();
void dump_stack_trace();
void dump_registers();

#ifdef __cplusplus
}
#endif


// Macros for better panic message tunning
#define BUG_ON(condition) \
    do { \
        if (unlikely(condition)) \
        { \
            panicex("BUG_ON: " #condition, PANIC_KERNEL_BUG, __FILE__, __LINE__, __func__); \
        } \
    } while(0)

/* For testing */
#define ASSERT(condition) \
    do { \
        if (unlikely(!(condition))) \
        { \
            panicex("ASSERT failed: " #condition, PANIC_ASSERT_FAILED, __FILE__, __LINE__, __func__); \
        } \
    } while(0)
      
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

