#ifndef MATLOG_MATH_HYPER_H
#define MATLOG_MATH_HYPER_H

#include <matlog/math/value.h>

/*
 * Complex numbers and quaternions (plan section 4.17), one representation for
 * both: four parts (a, b, c, d) for a + b i + c j + d k. A complex number has
 * c = d = 0, so complex numbers are the quaternions on the plane of i, and
 * i * j = k holds when a complex number meets a quaternion. A value whose
 * imaginary parts are all zero is a plain real number and is given as one.
 *
 * The parts are exact rationals as long as everything that made them is exact
 * (3 + 4i, 1 + 2i + 3j + 4k, sqrt (-4) = 2i), and doubles after an
 * approximation (exp (i pi), sqrt (-2)); an exact number meeting an
 * approximate one gives an approximate result.
 *
 *	+ -             part by part
 *	*               the Hamilton product; it does not commute for
 *	                quaternions (i j is k, j i is -k)
 *	/               a / b is a times the inverse of b, conj (b) / |b|^2
 *	^               an integer power by repeated products; a complex number
 *	                to any complex power through exp and ln (approximate,
 *	                the principal value)
 *	sqrt of a negative number is a complex number: sqrt (-4) is 2i
 *
 * The constants i, j and k are the units (a variable of that name hides
 * them). Functions: re im conj abs arg (complex), quat (w, x, y, z),
 * polar (r, theta) and rotate (q, v), which turns the 3 vector v by the
 * quaternion q: q v q^-1. exp ln sin cos take complex numbers too.
 *
 * Shown as `3 + 4i`, `-i`, `1/2 - (3/4)i`, `1 + 2i + 3j + 4k`; approximate
 * values with a leading sign, `≈ 1.5 + 2.5i`; an approximate part that is
 * tiny against the others (1e-16 of exp (i pi)) shows as zero.
 */

typedef enum MlHyperOp
{
	ML_HYPER_ADD = 0,
	ML_HYPER_MUL,
	ML_HYPER_DIV,
	ML_HYPER_POW
} MlHyperOp;

typedef enum MlHyperFn
{
	ML_HFN_RE = 0,
	ML_HFN_IM,
	ML_HFN_CONJ,
	ML_HFN_ABS,
	ML_HFN_ARG,
	ML_HFN_EXP,
	ML_HFN_LN,
	ML_HFN_SIN,
	ML_HFN_COS,
	ML_HFN_SQRT
} MlHyperFn;

/* 1 for a complex number or a quaternion. */
int32_t ml_value_is_hyper (MlValue v);

/* The parts of an exact or approximate number or of a hyper value. */
MlHyper ml_hyper_of (MlArena *arena, MlValue v);

/* The value for parts: a real number when the imaginary parts are zero. */
MlValue ml_hyper_value (MlHyper h);

/* Exact parts / approximate parts. */
MlHyper ml_hyper_exact (MlRat a, MlRat b, MlRat c, MlRat d);
MlHyper ml_hyper_approx (double a, double b, double c, double d);

/* a op b, one of them (at least) a complex number or a quaternion. */
MlValue ml_hyper_apply (MlArena *arena, MlHyperOp op, MlValue a, MlValue b);

MlValue ml_hyper_neg (MlArena *arena, MlValue a);

/* The square root of a negative exact number: 2i for -4. */
MlValue ml_hyper_sqrt_negative (MlArena *arena, MlRat negative);

/* A negative number to a power that is not a whole number (approximate). */
MlValue ml_hyper_pow_negative (MlArena *arena, MlValue base, MlValue power);

/* One of the functions of a complex number or a quaternion or a number. */
MlValue ml_hyper_function (MlArena *arena, MlHyperFn which, MlValue a);

/* quat (w, x, y, z), polar (r, theta) and rotate (q, v). */
MlValue ml_hyper_quat (MlArena *arena, const MlValue *args);
MlValue ml_hyper_polar (MlArena *arena, MlValue r, MlValue theta);
MlValue ml_hyper_rotate (MlArena *arena, MlValue q, MlValue v);

/* "3 + 4i", "≈ 1.5 + 2.5i". */
MlResult ml_hyper_format (MlStr *out, MlArena *arena, MlHyper h);

#endif /* MATLOG_MATH_HYPER_H */
