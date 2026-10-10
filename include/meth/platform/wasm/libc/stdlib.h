#ifndef METH_WASM_LIBC_STDLIB_H
#define METH_WASM_LIBC_STDLIB_H

#include <stddef.h>

/* There is no environment, so there are no variables. */
char *getenv (const char *name);
int setenv (const char *name, const char *value, int overwrite);
int atoi (const char *s);
int abs (int n);

#endif /* METH_WASM_LIBC_STDLIB_H */
