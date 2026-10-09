#ifndef MATLOG_MATH_MATRIX_H
#define MATLOG_MATH_MATRIX_H

#include <matlog/math/rational.h>

/*
 * Exact matrices: a rows x cols table of rationals stored row by row in an
 * arena. Like the numbers underneath, a matrix is a small struct passed by
 * value, and a failure is a matrix with a status that every operation passes
 * on, so a chain of operations is checked once at the end with ml_mat_ok.
 *
 *	ML_ERR_INVALID  sizes that do not fit the operation (a 2 x 3 times a
 *	                2 x 3, the determinant of a non square matrix), or a
 *	                size of zero
 *	ML_ERR_DIVZERO  a singular matrix has no inverse and no unique solution
 *	ML_ERR_LIMIT    more than ML_MAT_MAX_DIM rows or columns, or numbers that
 *	                got too large
 *	ML_ERR_NOMEM
 *
 * The operations do not change their arguments; only ml_mat_set writes into a
 * matrix, and only for building one.
 */

#define ML_MAT_MAX_DIM 64

typedef struct MlMat
{
	MlResult status;
	uint64_t rows;
	uint64_t cols;
	MlRat    *cells; /* rows * cols, row by row; arena memory */
} MlMat;

typedef struct MlRref
{
	MlMat    matrix; /* the reduced row echelon form */
	uint64_t rank;
} MlRref;

typedef struct MlRatResult
{
	MlResult status;
	MlRat    value;
} MlRatResult;

/* Construction. */
MlMat ml_mat_error (MlResult status);
MlMat ml_mat_zero (MlArena *arena, uint64_t rows, uint64_t cols);
MlMat ml_mat_identity (MlArena *arena, uint64_t n);

/* Cells. Out of range reads give an error number, writes are ignored. */
MlRat ml_mat_get (MlMat m, uint64_t row, uint64_t col);
void ml_mat_set (MlMat *m, uint64_t row, uint64_t col, MlRat value);

int32_t ml_mat_ok (MlMat m);
int32_t ml_mat_is_square (MlMat m);

/* 1 when every cell of a and b is equal (and the sizes). */
int32_t ml_mat_equal (MlArena *arena, MlMat a, MlMat b);

/* Arithmetic. */
MlMat ml_mat_add (MlArena *arena, MlMat a, MlMat b);
MlMat ml_mat_sub (MlArena *arena, MlMat a, MlMat b);
MlMat ml_mat_mul (MlArena *arena, MlMat a, MlMat b);
MlMat ml_mat_scale (MlArena *arena, MlMat a, MlRat k);
MlMat ml_mat_neg (MlArena *arena, MlMat a);
MlMat ml_mat_transpose (MlArena *arena, MlMat a);

/* Square matrices. */
MlRatResult ml_mat_trace (MlArena *arena, MlMat a);
MlRatResult ml_mat_det (MlArena *arena, MlMat a);
MlMat ml_mat_inverse (MlArena *arena, MlMat a);

/*
 * A to an integer power by repeated squaring; a negative power uses the
 * inverse. The 0th power is the identity.
 */
MlMat ml_mat_pow (MlArena *arena, MlMat a, int64_t exponent);

/* Gauss-Jordan elimination: the reduced row echelon form and the rank. */
MlRref ml_mat_rref (MlArena *arena, MlMat a);

/*
 * The solution x of a x = b for a square a and b with the same number of
 * rows. A singular a is ML_ERR_DIVZERO, whether there is no solution or
 * many.
 */
MlMat ml_mat_solve (MlArena *arena, MlMat a, MlMat b);

/* "[[1, 2], [3, 4]]", cells as rationals ("1/2"). */
MlResult ml_mat_format (MlStr *out, MlMat m);

#endif /* MATLOG_MATH_MATRIX_H */
