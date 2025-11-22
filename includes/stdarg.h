#ifndef STDARG_H
#define STDARG_H

// Use GCC builtins for variable arguments
// This is required for x86-64 System V ABI which passes arguments in registers
typedef __builtin_va_list va_list;

#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)
#define va_copy(dest, src) __builtin_va_copy(dest, src)

#endif // STDARG_H
