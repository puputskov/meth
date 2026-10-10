#ifndef METH_CORE_STR_H
#define METH_CORE_STR_H

#include <stddef.h>
#include <stdint.h>

#include <meth/core/err.h>
#include <meth/core/slice.h>

/*
 * Growable, NUL-terminated byte string (UTF-8 by convention).
 * data is NULL until the first allocation. Read-only operations take the
 * string by value; mutating ones take a pointer to it.
 */
typedef struct MlStr
{
	char     *data;
	uint64_t len; /* bytes, excluding the terminator */
	uint64_t cap; /* allocated bytes, including the terminator */
} MlStr;

void ml_str_init (MlStr *s);
void ml_str_free (MlStr *s);
void ml_str_clear (MlStr *s);

/* View of the current contents. Invalidated by any mutating call. */
MlSlice ml_str_view (MlStr s);

/*
 * Valid C string even for a fresh, empty string. Invalidated by any mutating
 * call.
 */
const char *ml_str_cstr (MlStr s);

/* Ensures room for len bytes plus the terminator. */
MlResult ml_str_reserve (MlStr *s, uint64_t len);

/* text must not point into s. */
MlResult ml_str_append (MlStr *s, MlSlice text);
MlResult ml_str_append_cp (MlStr *s, uint32_t cp);

/* printf style append */
MlResult ml_str_appendf (MlStr *s, const char *fmt, ...);

/* Insert text at byte offset pos (pos <= len). text must not point into s. */
MlResult ml_str_insert (MlStr *s, uint64_t pos, MlSlice text);

/* Erase n bytes starting at byte offset pos (clamped to the end). */
MlResult ml_str_erase (MlStr *s, uint64_t pos, uint64_t n);

#endif /* METH_CORE_STR_H */
