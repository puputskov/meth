#ifndef MATLOG_MATH_VALUE_H
#define MATLOG_MATH_VALUE_H

#include <matlog/math/matrix.h>
#include <matlog/math/unit.h>
#include <matlog/sym/expr.h>

/*
 * The result of evaluating something. That is a number:
 *
 *	exact    a rational of bignums, no information is lost
 *	approx   a double; it comes from a result that is not a rational (sqrt (2),
 *	         sin (1), pi) and it spreads: exact with approx gives approx
 *
 * or an exact quantity (a number of coherent SI units with a dimension, see
 * unit.h), or an exact matrix (a vector is a matrix of one column, see
 * matrix.h), or an
 * error, which carries what went wrong and the byte range of the source
 * text it belongs to. Like the numbers underneath, a value is a small struct
 * passed by value (arena memory for the digits), and an error given to any
 * operation comes back out of it, so an expression is checked once at the end.
 */

typedef enum MlValueKind
{
	ML_VALUE_NONE = 0, /* nothing to show (a definition, a blank line) */
	ML_VALUE_EXACT,
	ML_VALUE_APPROX,
	ML_VALUE_MATRIX, /* exact cells, see matrix.h */
	ML_VALUE_EXPR,   /* a symbolic expression, see sym/expr.h */
	ML_VALUE_QUANTITY, /* exact value in SI units and a dimension */
	ML_VALUE_ERROR
} MlValueKind;

typedef enum MlErrorKind
{
	ML_EVAL_NONE = 0,
	ML_EVAL_SYNTAX,         /* the text is not a valid expression */
	ML_EVAL_DIV_ZERO,       /* division by zero */
	ML_EVAL_ZERO_POW_ZERO,  /* 0 to the power 0 */
	ML_EVAL_NOT_REAL,       /* the result is not a real number */
	ML_EVAL_DOMAIN,         /* argument outside what the function accepts */
	ML_EVAL_NOT_INTEGER,    /* an integer is needed */
	ML_EVAL_UNDEFINED_NAME, /* variable or function that does not exist */
	ML_EVAL_NOT_FUNCTION,   /* a call of something that is not a function */
	ML_EVAL_NOT_VALUE,      /* a function used where a number is needed */
	ML_EVAL_ARITY,          /* wrong number of arguments */
	ML_EVAL_TOO_LARGE,      /* the result got too large */
	ML_EVAL_DEPTH,          /* recursion went too deep */
	ML_EVAL_NO_FILE,        /* an included file cannot be read */
	ML_EVAL_INCLUDE_LOOP,   /* a file includes itself, directly or not */
	ML_EVAL_SIZE,           /* matrix sizes that do not fit the operation */
	ML_EVAL_TYPE,           /* operands of a kind the operation cannot use */
	/* an exact number is needed, not an approximation */
	ML_EVAL_NOT_EXACT,
	ML_EVAL_CANNOT_INTEGRATE, /* no closed form found */
	ML_EVAL_NOT_SYMBOLIC,   /* not a formula the symbolic engine can use */
	ML_EVAL_SINGULAR,       /* a singular matrix has no inverse */
	ML_EVAL_DIMENSION_MISMATCH, /* units of different dimensions combined */
	ML_EVAL_UNKNOWN_UNIT,   /* a unit name that does not exist */
	ML_EVAL_NO_MEMORY,
	ML_EVAL_KIND_COUNT
} MlErrorKind;

typedef struct MlValue
{
	MlValueKind kind;
	MlRat       exact;  /* ML_VALUE_EXACT, ML_VALUE_QUANTITY (in SI units) */
	MlDim       dim;    /* ML_VALUE_QUANTITY */
	MlSlice     show;   /* quantity: the unit to show it in (after "to") */
	MlRat       scale;  /* quantity: SI value of one show, if show is set */
	MlMat       matrix; /* ML_VALUE_MATRIX */
	const MlSym *sym;   /* ML_VALUE_EXPR */
	double      approx; /* ML_VALUE_APPROX */
	MlErrorKind error;  /* ML_VALUE_ERROR */
	/* byte range in the source, error_len 0 if unknown */
	uint64_t error_start;
	uint64_t error_len;
} MlValue;

typedef struct MlI64Result
{
	int32_t ok; /* 1 when the value is an exact integer that fits */
	int64_t value;
} MlI64Result;

/* Construction. */
MlValue ml_value_none (void);
MlValue ml_value_exact (MlRat x);
MlValue ml_value_approx (double x);

/*
 * A quantity: si is the number of coherent SI units (5 km is 5000 with the
 * dimension of a length). A quantity without a dimension is an exact number.
 */
MlValue ml_value_quantity (MlRat si, MlDim dim);
int32_t ml_value_is_quantity (MlValue v);

/*
 * A matrix value. A matrix that carries a failure becomes the matching error:
 * sizes that do not fit are ML_EVAL_SIZE, a singular matrix ML_EVAL_SINGULAR.
 */
MlValue ml_value_matrix (MlMat m);

/*
 * A symbolic expression as a value. A number stays an exact number; a
 * failed context (see sym/expr.h) becomes the matching error: no room is
 * ML_EVAL_TOO_LARGE, a division by zero ML_EVAL_DIV_ZERO.
 */
MlValue ml_value_expr (const MlSym *s, MlResult status);
int32_t ml_value_is_expr (MlValue v);
MlValue ml_value_from_i64 (MlArena *arena, int64_t n);
MlValue ml_value_error (MlErrorKind kind, uint64_t start, uint64_t len);

/* Gives an error that has no position yet the range [start, start + len). */
MlValue ml_value_locate (MlValue v, uint64_t start, uint64_t len);

/* The error kind for a failed bignum or rational operation. */
MlErrorKind ml_error_from_result (MlResult status);

/* Inspection. */
int32_t ml_value_is_error (MlValue v);
int32_t ml_value_is_number (MlValue v);
int32_t ml_value_is_exact (MlValue v);
int32_t ml_value_is_matrix (MlValue v);
MlI64Result ml_value_to_i64 (MlValue v);

/* The value as a double (0 for values that are not numbers). */
double ml_value_to_double (MlArena *arena, MlValue v);

/*
 * -1, 0 or 1 for two numbers. Exact numbers are compared exactly. Quantities
 * compare their SI values; the caller checks that the dimensions are equal.
 */
int32_t ml_value_cmp (MlArena *arena, MlValue a, MlValue b);

/*
 * Arithmetic. Errors come back out; see the file comment. With matrices:
 *
 *	matrix + matrix, -         same sizes (ML_EVAL_SIZE otherwise)
 *	matrix * matrix            the product; number * matrix scales it
 *	matrix / number            scales by the inverse of an exact number
 *	matrix ^ integer           repeated product, ^-1 is the inverse
 *
 * Quantities add and subtract only with the same dimension (zero counts as a
 * quantity of any dimension; anything else is ML_EVAL_DIMENSION_MISMATCH),
 * multiply and divide by adding and subtracting dimensions, and take an
 * integer power. They are exact: an approximate number meeting one is
 * ML_EVAL_NOT_EXACT, a matrix or expression ML_EVAL_TYPE.
 *
 * Any other mix (a number plus a matrix, a number divided by a matrix, a power
 * that is not an integer) is ML_EVAL_TYPE, and an approximate number meeting a
 * matrix is ML_EVAL_NOT_EXACT.
 */
MlValue ml_value_neg (MlArena *arena, MlValue a);
MlValue ml_value_add (MlArena *arena, MlValue a, MlValue b);
MlValue ml_value_sub (MlArena *arena, MlValue a, MlValue b);
MlValue ml_value_mul (MlArena *arena, MlValue a, MlValue b);
MlValue ml_value_div (MlArena *arena, MlValue a, MlValue b);

/*
 * a to the power b. An integer power of an exact number is exact, so is
 * the square root of a perfect square (power 1/2); any other power is
 * approximate. 0^0 is an error, a negative number to a fraction is not real.
 */
MlValue ml_value_pow (MlArena *arena, MlValue a, MlValue b);

/*
 * "7/2" or "-3" for exact numbers, "≈ 1.41421356" (nine significant digits)
 * for approximations, "[[1, 2], [3, 4]]" for matrices. Values that are
 * neither cannot be formatted.
 */
MlResult ml_value_format (MlStr *out, MlArena *arena, MlValue v);

/*
 * The stable machine readable name of an error kind: "div_zero",
 * "undefined_name", "singular" ... (the enum name in lower case without the
 * ML_EVAL_ prefix). Scripts and the JSON output of the command line use it.
 */
const char *ml_error_code (MlErrorKind kind);

/* The message of an error, with the offending name for undefined names. */
const char *ml_error_text (MlErrorKind kind);
MlResult ml_error_message (MlStr *out, MlValue v, MlSlice source);

#endif /* MATLOG_MATH_VALUE_H */
