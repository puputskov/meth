#ifndef MATLOG_MATH_PGA_H
#define MATLOG_MATH_PGA_H

#include <matlog/math/value.h>

/*
 * Projective geometric algebra (plan section 4, milestone 7). The algebra of
 * the plane is R(2,0,1) and the algebra of space R(3,0,1): the basis vectors
 * e0, e1, e2 (and e3), where e1, e2, e3 square to +1 and e0 squares to 0 (it
 * is the degenerate direction that makes the algebra projective). One
 * implementation serves both: a basis blade is a bit mask (bit i is the basis
 * vector e_i), the metric is the diagonal 0, 1, 1, 1, and a multivector is a
 * dense list of coefficients, one for each of the 2^(dim + 1) blades.
 *
 * This is plane based PGA: a vector is a plane (or, in 2D, a line)
 * a e1 + b e2 + c e0 meaning the points x with a x + b y + c = 0 (and in 3D
 * a e1 + b e2 + c e3 + d e0), a bivector is a line (in 2D a point), and in 3D a
 * trivector is a point:
 *
 *	2D	point (x, y)     = e12 + x e20 + y e01
 *	3D	point (x, y, z)  = e123 + x e032 + y e013 + z e021
 *
 * The products:
 *	a * b	the geometric product (an even element, a motor, is a rotation, a
 *		translation or a screw motion; m * x * reverse (m) moves any
 *		object x, a point, a line or a plane, in the same way)
 *	a ^ b	the outer product: the meet of two planes is their line
 *	join (a, b)	the regressive product: the join of two points is their line
 *	inner (a, b)	the part of a * b of the grade |grade a - grade b|
 *
 * The coefficients are exact rationals as long as everything that made them is
 * exact, and doubles after an approximation (a rotation by an angle); an exact
 * multivector meeting an approximate one gives an approximate result. A
 * multivector with only a scalar part is a plain number.
 */

#define ML_MV_MAX_BLADES 16

typedef struct MlMv
{
	int32_t dim;    /* 2 or 3: R(dim, 0, 1) has dim + 1 basis vectors */
	int32_t approx; /* 1: the coefficients are in d, else in r */
	MlRat   r [ML_MV_MAX_BLADES];
	double  d [ML_MV_MAX_BLADES];
} MlMv;

typedef enum MlMvOp
{
	ML_MV_ADD = 0,
	ML_MV_SUB,
	ML_MV_MUL,
	ML_MV_DIV,
	ML_MV_WEDGE,
	ML_MV_POW,
	ML_MV_INNER,
	ML_MV_JOIN
} MlMvOp;

typedef enum MlMvFn
{
	ML_MVFN_REVERSE = 0,
	ML_MVFN_DUAL,
	ML_MVFN_UNDUAL,
	ML_MVFN_INVERSE,
	ML_MVFN_NORM,
	ML_MVFN_UNIT,
	ML_MVFN_SCALAR
} MlMvFn;

typedef enum MlMvMake
{
	ML_MVMAKE_POINT = 0,
	ML_MVMAKE_LINE,
	ML_MVMAKE_PLANE,
	ML_MVMAKE_TRANSLATOR,
	ML_MVMAKE_ROTOR
} MlMvMake;

/* 1 for a multivector. */
int32_t ml_value_is_mv (MlValue v);

/*
 * The basis blade a name stands for: e12, e0, e021 (the digits are the basis
 * vectors, in any order: e21 is -e12), as a value; ML_VALUE_NONE when the
 * name is not a blade of the algebra of the dimension dim (2 or 3).
 */
MlValue ml_mv_blade (MlArena *arena, int32_t dim, MlSlice name);

/* 1 when the name is a blade of the algebra of the dimension dim. */
int32_t ml_mv_is_blade (int32_t dim, MlSlice name);

/* a op b, one of them (at least) a multivector. */
MlValue ml_mv_apply (MlArena *arena, MlMvOp op, MlValue a, MlValue b);
MlValue ml_mv_neg (MlArena *arena, MlValue a);

/* A function of one multivector. */
MlValue ml_mv_function (MlArena *arena, MlMvFn fn, MlValue a);

/* The part of the grade k (k a whole number). */
MlValue ml_mv_grade (MlArena *arena, MlValue a, MlValue k);

/*
 * Objects and motors from numbers; the number of arguments tells the
 * dimension. point (x, y) / point (x, y, z), line (a, b, c): a x + b y + c = 0,
 * plane (a, b, c, d), translator (dx, dy) / (dx, dy, dz), rotor (angle) in the
 * plane about the origin or rotor (angle, nx, ny, nz) in space about the axis
 * through the origin with the direction n; the angle is in radians.
 */
MlValue ml_mv_make (
	MlArena       *arena,
	MlMvMake      what,
	const MlValue *args,
	uint64_t      argc
);

/* Shown as 1 + 2e1 - (3/2)e12, approximate values with a leading sign. */
MlResult ml_mv_format (MlStr *out, MlArena *arena, const MlMv *m);

/* A copy of the multivector in the arena (for an environment). */
MlValue ml_mv_copy (MlArena *arena, MlValue v);

#endif /* MATLOG_MATH_PGA_H */
