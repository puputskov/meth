#ifndef METH_WASM_LIBC_STDIO_H
#define METH_WASM_LIBC_STDIO_H

#include <stdarg.h>
#include <stddef.h>

/*
 * snprintf and vsnprintf with the conversions the program uses: %d %i %u %x
 * %X %c %s %g and %%, the flags - and 0, a width and a precision (also *),
 * and the length modifiers l, ll, z and h (ignored: the arguments are read
 * by the conversion).
 */
int snprintf (char *buf, size_t cap, const char *fmt, ...);
int vsnprintf (char *buf, size_t cap, const char *fmt, va_list ap);

/*
 * printf writes to the page (the import console_write of the module), the only
 * output there is; fflush has nothing to do. Used by the tests.
 */
#define stdout ((void *)0)
int printf (const char *fmt, ...);
int fflush (void *stream);

#endif /* METH_WASM_LIBC_STDIO_H */
