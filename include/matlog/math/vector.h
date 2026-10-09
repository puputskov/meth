#ifndef MATLOG_MATH_VECTOR_H
#define MATLOG_MATH_VECTOR_H

#include <matlog/math/matrix.h>

/*
 * Vectors are columns: matrices with one column (n x 1), so they take part in
 * every matrix operation (a matrix times a vector, a vector scaled) without a
 * type of their own. The functions here need that shape and give
 * ML_ERR_INVALID for anything else, or for two vectors of different length.
 */

/* A vector from n integers. */
MlMat ml_vec_from_i64 (MlArena *arena, uint64_t n, const int64_t *values);

int32_t ml_vec_is_vector (MlMat v);

/* The dot product. */
MlRatResult ml_vec_dot (MlArena *arena, MlMat a, MlMat b);

/* The cross product of two vectors of length 3. */
MlMat ml_vec_cross (MlArena *arena, MlMat a, MlMat b);

/* The squared length: the dot product of a vector with itself. */
MlRatResult ml_vec_norm_squared (MlArena *arena, MlMat v);

typedef struct MlVecNorm
{
	MlResult status;
	int32_t  exact; /* 1 when the length is a rational: value is it */
	MlRat    value; /* the length if exact, else the squared length */
} MlVecNorm;

/*
 * The length. A rational length (3-4-5) is given exactly; otherwise the
 * caller gets the squared length and takes the root as an approximation.
 */
MlVecNorm ml_vec_norm (MlArena *arena, MlMat v);

#endif /* MATLOG_MATH_VECTOR_H */
