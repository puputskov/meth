#ifndef METH_CORE_BIGINT_H
#define METH_CORE_BIGINT_H

#include <meth/core/err.h>
#include <meth/core/mem.h>
#include <meth/core/slice.h>
#include <meth/core/str.h>

/*
 * Signed integers of arbitrary size, up to ML_BIG_MAX_LIMBS limbs.
 *
 * A value is a small struct that is passed and returned by value. Its limbs
 * live in the arena that was given to the operation that made it, so nothing
 * is freed one by one: reset or free the arena when the values are not needed.
 * Operations never change their arguments.
 *
 * A value carries a status. It is ML_OK for a number; operations that fail
 * (too large, division by zero, a bad digit string, no memory) return a value
 * whose status says why, and any operation with such a value as an argument
 * returns the same status. That way a long expression is checked once, at the
 * end, with ml_big_ok.
 */

/* 32 bit limbs: 3200 limbs hold numbers of about 100000 bits (30000 digits). */
#define ML_BIG_MAX_LIMBS 3200

typedef struct MlBig
{
	const uint32_t *limbs;   /* magnitude, least significant limb first */
	uint64_t       len;      /* limbs without leading zeros, 0 for zero */
	int32_t        negative; /* 1 when below zero, never set for zero */
	MlResult       status;
} MlBig;

typedef struct MlBigDiv
{
	MlBig quot;
	MlBig rem;
} MlBigDiv;

typedef struct MlU64Result
{
	int32_t  ok; /* 1 when the value fits */
	uint64_t value;
} MlU64Result;

/* Construction. */
MlBig ml_big_zero (void);
MlBig ml_big_from_u64 (MlArena *arena, uint64_t value);
MlBig ml_big_from_i64 (MlArena *arena, int64_t value);

/* A copy of x whose limbs live in the arena (so x may be freed or reset). */
MlBig ml_big_copy (MlArena *arena, MlBig x);

/* Optional sign, then decimal digits. ML_ERR_PARSE for anything else. */
MlBig ml_big_parse (MlArena *arena, MlSlice text);

/* A value with the given failure status (and the value zero). */
MlBig ml_big_error (MlResult status);

/* Inspection. */
int32_t ml_big_ok (MlBig x);
int32_t ml_big_is_zero (MlBig x);
int32_t ml_big_is_odd (MlBig x);

/* -1, 0 or 1. Only meaningful for values with status ML_OK. */
int32_t ml_big_cmp (MlBig x, MlBig y);

/* Number of bits of the magnitude (0 for zero). */
uint64_t ml_big_bit_length (MlBig x);

MlU64Result ml_big_to_u64 (MlBig x);

/* The nearest double, or an infinity when it does not fit. */
double ml_big_to_double (MlBig x);

/* Appends the decimal digits (with a leading '-') to out. */
MlResult ml_big_format (MlStr *out, MlBig x);

/* Arithmetic. */
MlBig ml_big_neg (MlArena *arena, MlBig x);
MlBig ml_big_abs (MlArena *arena, MlBig x);
MlBig ml_big_add (MlArena *arena, MlBig x, MlBig y);
MlBig ml_big_sub (MlArena *arena, MlBig x, MlBig y);
MlBig ml_big_mul (MlArena *arena, MlBig x, MlBig y);

/*
 * Division truncated toward zero: x = quot * y + rem, |rem| < |y|, and rem
 * has the sign of x. A zero divisor gives ML_ERR_DIVZERO in both parts.
 */
MlBigDiv ml_big_divmod (MlArena *arena, MlBig x, MlBig y);

/* Greatest common divisor, never negative. gcd (0, 0) is 0. */
MlBig ml_big_gcd (MlArena *arena, MlBig x, MlBig y);

/* x to a non negative power. ML_ERR_LIMIT as soon as it gets too large. */
MlBig ml_big_pow (MlArena *arena, MlBig x, uint64_t exponent);

/*
 * Shifts of the magnitude (the sign is kept, so right shifts round toward
 * zero).
 */
MlBig ml_big_shl (MlArena *arena, MlBig x, uint64_t bits);
MlBig ml_big_shr (MlArena *arena, MlBig x, uint64_t bits);

/* The integer square root, rounded down. ML_ERR_RANGE for a negative x. */
MlBig ml_big_isqrt (MlArena *arena, MlBig x);

#endif /* METH_CORE_BIGINT_H */
