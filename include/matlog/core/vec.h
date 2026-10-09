#ifndef MATLOG_CORE_VEC_H
#define MATLOG_CORE_VEC_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <matlog/core/err.h>

/*
 * Typed growable array, generated per element type so elements are passed
 * and returned by value and no void pointers appear in the API.
 *
 *	ML_VEC_DEFINE (MlIntVec, int32_t)
 *
 * generates the type MlIntVec and these functions (Name is the type name):
 *
 *	void     Name_init (Name *v);
 *	void     Name_free (Name *v);
 *	void     Name_clear (Name *v);
 *	MlResult Name_reserve (Name *v, uint64_t cap);
 *	MlResult Name_push (Name *v, T value);
 *	MlResult Name_insert (Name *v, uint64_t idx, T value);	idx == len appends
 *	MlResult Name_set (Name *v, uint64_t idx, T value);
 *	MlResult Name_remove (Name *v, uint64_t idx);
 *	MlResult Name_pop (Name *v);
 *	T        Name_get (const Name *v, uint64_t idx);
 *		idx out of range gives a zeroed T
 *
 * The container itself is always passed by pointer, since it is mutated.
 */

#define ML_VEC_DEFINE(Name, T) \
typedef struct Name \
{ \
	T        *data; \
	uint64_t len; \
	uint64_t cap; \
} Name; \
\
static inline void Name##_init (Name *v) \
{ \
	v->data = NULL; \
	v->len  = 0; \
	v->cap  = 0; \
} \
\
static inline void Name##_free (Name *v) \
{ \
	free (v->data); \
	Name##_init (v); \
} \
\
static inline void Name##_clear (Name *v) \
{ \
	v->len = 0; \
} \
\
static inline MlResult Name##_reserve (Name *v, uint64_t cap) \
{ \
	MlResult r = ML_OK; \
\
	if (cap > v->cap) \
	{ \
		T *p = NULL; \
\
		if (cap > SIZE_MAX / sizeof (T)) \
		{ \
			r = ML_ERR_NOMEM; \
		} \
\
		else \
		{ \
			p = (T *)realloc (v->data, cap * sizeof (T)); \
			if (p) \
			{ \
				v->data = p; \
				v->cap  = cap; \
			} \
\
			else \
			{ \
				r = ML_ERR_NOMEM; \
			} \
		} \
	} \
\
	return r; \
} \
\
static inline MlResult Name##_insert (Name *v, uint64_t idx, T value) \
{ \
	MlResult r = ml_choose ( \
		idx <= v->len, \
		ML_OK, \
		ML_ERR_RANGE \
	); \
\
	if (r == ML_OK && v->len == v->cap) \
	{ \
		r = Name##_reserve ( \
			v, \
			ml_choose ( \
				v->cap > 0, \
				v->cap * 2, \
				8 \
			) \
		); \
	} \
\
	if (r == ML_OK) \
	{ \
		memmove ( \
			v->data + idx + 1, \
			v->data + idx, \
			(v->len - idx) * sizeof (T) \
		); \
		v->data [idx] = value; \
		v->len++; \
	} \
\
	return r; \
} \
\
static inline MlResult Name##_push (Name *v, T value) \
{ \
	return Name##_insert (v, v->len, value); \
} \
\
static inline MlResult Name##_set (Name *v, uint64_t idx, T value) \
{ \
	MlResult r = ml_choose ( \
		idx < v->len, \
		ML_OK, \
		ML_ERR_RANGE \
	); \
\
	if (r == ML_OK) \
	{ \
		v->data [idx] = value; \
	} \
\
	return r; \
} \
\
static inline MlResult Name##_remove (Name *v, uint64_t idx) \
{ \
	MlResult r = ml_choose ( \
		idx < v->len, \
		ML_OK, \
		ML_ERR_RANGE \
	); \
\
	if (r == ML_OK) \
	{ \
		memmove ( \
			v->data + idx, \
			v->data + idx + 1, \
			(v->len - idx - 1) * sizeof (T) \
		); \
		v->len--; \
	} \
\
	return r; \
} \
\
static inline MlResult Name##_pop (Name *v) \
{ \
	return ml_choose ( \
		v->len > 0, \
		Name##_remove (v, v->len - 1), \
		ML_ERR_RANGE \
	); \
} \
\
static inline T Name##_get (const Name *v, uint64_t idx) \
{ \
	T zero; \
\
	memset (&zero, 0, sizeof zero); \
	return ml_choose ( \
		idx < v->len, \
		v->data [idx], \
		zero \
	); \
}

#endif /* MATLOG_CORE_VEC_H */
