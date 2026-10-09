#ifndef MATLOG_MATH_RATIONAL_H
#define MATLOG_MATH_RATIONAL_H

#include <matlog/core/bigint.h>

/*
 * Exact fractions of bignum integers. A value is always in lowest terms with a
 * positive denominator (zero is 0/1). Like MlBig, a rational is a small struct
 * passed by value whose digits live in an arena, and a failure (division by
 * zero, too large, no memory) is a value with a status that every operation
 * passes on; check it once at the end with ml_rat_ok.
 */

typedef struct MlRat
{
	MlBig num;
	MlBig den;
} MlRat;

typedef struct MlRatSqrt
{
	int32_t exact; /* 1 when the root is itself a rational */
	MlRat   value; /* the root when exact */
} MlRatSqrt;

typedef struct MlRatDecimal
{
	MlResult result;
	int32_t  exact; /* 1 when the expansion ended, 0 when it was cut off */
} MlRatDecimal;

/* Construction. */
MlRat ml_rat_error (MlResult status);
MlRat ml_rat_zero (MlArena *arena);
MlRat ml_rat_from_big (MlArena *arena, MlBig n);
MlRat ml_rat_from_i64 (MlArena *arena, int64_t n);

/* num / den reduced to lowest terms. A zero denominator is ML_ERR_DIVZERO. */
MlRat ml_rat_make (MlArena *arena, MlBig num, MlBig den);

/*
 * Plain decimal text: digits with an optional fraction ("0.75", ".5", "12")
 * and an optional sign. No exponent. ML_ERR_PARSE for anything else.
 */
MlRat ml_rat_parse_decimal (MlArena *arena, MlSlice text);

/* Inspection. */
MlResult ml_rat_status (MlRat x);
int32_t ml_rat_ok (MlRat x);
int32_t ml_rat_is_zero (MlRat x);
int32_t ml_rat_is_integer (MlRat x);
int32_t ml_rat_sign (MlRat x); /* -1, 0 or 1 */

/* -1, 0 or 1. Only meaningful for values with status ML_OK. */
int32_t ml_rat_cmp (MlArena *arena, MlRat x, MlRat y);

/* Arithmetic. Division by zero is ML_ERR_DIVZERO. */
MlRat ml_rat_neg (MlArena *arena, MlRat x);
MlRat ml_rat_abs (MlArena *arena, MlRat x);
MlRat ml_rat_add (MlArena *arena, MlRat x, MlRat y);
MlRat ml_rat_sub (MlArena *arena, MlRat x, MlRat y);
MlRat ml_rat_mul (MlArena *arena, MlRat x, MlRat y);
MlRat ml_rat_div (MlArena *arena, MlRat x, MlRat y);
MlRat ml_rat_recip (MlArena *arena, MlRat x);

/*
 * x to an integer power. A negative power of zero is ML_ERR_DIVZERO; zero to
 * the power zero is 1 here, callers that want it to be an error check first.
 */
MlRat ml_rat_pow (MlArena *arena, MlRat x, int64_t exponent);

/* The square root when it is a rational (both parts perfect squares). */
MlRatSqrt ml_rat_sqrt_exact (MlArena *arena, MlRat x);

/* The nearest double; huge and tiny parts are scaled so the ratio is right. */
double ml_rat_to_double (MlArena *arena, MlRat x);

/* "7/2", or "3" for an integer. */
MlResult ml_rat_format (MlStr *out, MlRat x);

/*
 * The decimal expansion, truncated (not rounded) after max_digits fraction
 * digits. "3.5" is exact, "0.3333333333" for 1/3 with ten digits is not.
 */
MlRatDecimal ml_rat_format_decimal (
	MlStr    *out,
	MlArena  *arena,
	MlRat    x,
	uint64_t max_digits
);

#endif /* MATLOG_MATH_RATIONAL_H */
