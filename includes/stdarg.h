#ifndef STDARG_H
#define STDARG_H

// Simple va_list implementation for kernel
typedef char *va_list;

#define va_start(ap, last) ((ap) = (char *)&(last) + sizeof(last))
#define va_arg(ap, type) (*(type *)((ap) += sizeof(type), (ap) - sizeof(type)))
#define va_end(ap) ((ap) = (char *)0)

#endif // STDARG_H
