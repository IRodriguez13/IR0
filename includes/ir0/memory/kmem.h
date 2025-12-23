#pragma once
#include <stdint.h>
#include <stddef.h>


#ifdef __cplusplus
extern "C" {
#endif

// Internal implementations (pure logic, no validation)
void *__kmalloc_impl(size_t size);
void __kfree_impl(void *ptr);
void *__krealloc_impl(void *ptr, size_t new_size);
void *__kmalloc_aligned_impl(size_t size, size_t alignment);
void __kfree_aligned_impl(void *ptr);

// Checked wrappers with debug info
void *__kmalloc_checked(size_t size, const char *file, int line, const char *caller);
void __kfree_checked(void *ptr, const char *file, int line, const char *caller);
void *__krealloc_checked(void *ptr, size_t new_size, 
                        const char *file, int line, const char *caller);
void *__kmalloc_aligned_checked(size_t size, size_t alignment,
                               const char *file, int line, const char *caller);
void __kfree_aligned_checked(void *ptr, const char *file, int line, const char *caller);

void heap_init(void);
void kmem_stats(void);  /* Print memory statistics (debug only) */



#ifdef __cplusplus
}
#endif

// Automatic debug tracking macros - capture caller location transparently
#define kmalloc(size) \
    __kmalloc_checked((size), __FILE__, __LINE__, __func__)

#define kfree(ptr) \
    __kfree_checked((ptr), __FILE__, __LINE__, __func__)

#define krealloc(ptr, new_size) \
    __krealloc_checked((ptr), (new_size), __FILE__, __LINE__, __func__)

#define kmalloc_aligned(size, alignment) \
    __kmalloc_aligned_checked((size), (alignment), __FILE__, __LINE__, __func__)

#define kfree_aligned(ptr) \
    __kfree_aligned_checked((ptr), __FILE__, __LINE__, __func__)


