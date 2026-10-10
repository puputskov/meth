#ifndef METH_CORE_SLICE_H
#define METH_CORE_SLICE_H

#include <meth/core/types.h>

/*
 * Read-only view of bytes. Passed and returned by value; it does not own the
 * memory and is valid only while the underlying buffer is.
 */
typedef struct MlSlice
{
	const char *data;
	uint64_t   len;
} MlSlice;

MlSlice ml_slice (const char *data, uint64_t len);

/* View of a NUL-terminated C string (NULL gives an empty slice). */
MlSlice ml_slice_cstr (const char *cstr);

/* View of the n bytes at pos, clamped to the end of s. */
MlSlice ml_slice_sub (MlSlice s, uint64_t pos, uint64_t n);

/* 1 when both slices hold the same bytes. */
int32_t ml_slice_equal (MlSlice a, MlSlice b);

#endif /* METH_CORE_SLICE_H */
