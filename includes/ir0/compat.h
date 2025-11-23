#include <ir0/oops.h>
#include <stddef.h>


#ifdef __cplusplus
// Global new/delete operators
void* operator_new(size_t size);
void operator_delete(void* ptr) noexcept;
void operator_delete(void* ptr, size_t size) noexcept;


// Array new/delete operators
void* operator_new_array(size_t size);
void operator_delete_array(void* ptr) noexcept;
void operator_delete_array(void* ptr, size_t size) noexcept;

// C++ runtime support functions
extern "C" void __cxa_pure_virtual(void);
extern "C" int __cxa_guard_acquire(unsigned long long *guard);
extern "C" void __cxa_guard_release(unsigned long long *guard);
extern "C" void __cxa_guard_abort(unsigned long long *guard);

#else
// C linkage declarations (no operators)
#endif
