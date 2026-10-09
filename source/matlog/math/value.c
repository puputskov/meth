#include <matlog/math/value.h>

#include <math.h>

/* Approximations are written with this many significant digits. */
#define APPROX_DIGITS 9

static const char *const error_texts [ML_EVAL_KIND_COUNT] =
{
	[ML_EVAL_NONE]           = "no error",
	[ML_EVAL_SYNTAX]         = "syntax error",
	[ML_EVAL_DIV_ZERO]       = "division by zero",
	[ML_EVAL_ZERO_POW_ZERO]  = "0 to the power 0 is undefined",
	[ML_EVAL_NOT_REAL]       = "the result is not a real number",
	[ML_EVAL_DOMAIN]         = "argument outside the domain of the function",
	[ML_EVAL_NOT_INTEGER]    = "an integer is needed",
	[ML_EVAL_UNDEFINED_NAME] = "undefined name",
	[ML_EVAL_NOT_FUNCTION]   = "not a function",
	[ML_EVAL_NOT_VALUE]      = "a function is not a number",
	[ML_EVAL_ARITY]          = "wrong number of arguments",
	[ML_EVAL_TOO_LARGE]      = "value too large",
	[ML_EVAL_DEPTH]          = "recursion too deep",
	[ML_EVAL_NO_FILE]        = "included file not found",
	[ML_EVAL_INCLUDE_LOOP]   = "include cycle",
	[ML_EVAL_NO_MEMORY]      = "out of memory"
};

const char *ml_error_text (MlErrorKind kind)
{
	return ml_choose (
		(int32_t)kind >= 0 && kind < ML_EVAL_KIND_COUNT,
		error_texts [kind],
		"unknown error"
	);
}

MlErrorKind ml_error_from_result (MlResult status)
{
	MlErrorKind kind = ML_EVAL_SYNTAX;

	if (status == ML_ERR_DIVZERO)
	{
		kind = ML_EVAL_DIV_ZERO;
	}

	else if (status == ML_ERR_LIMIT)
	{
		kind = ML_EVAL_TOO_LARGE;
	}

	else if (status == ML_ERR_NOMEM)
	{
		kind = ML_EVAL_NO_MEMORY;
	}

	else if (status == ML_ERR_RANGE)
	{
		kind = ML_EVAL_DOMAIN;
	}

	return kind;
}

MlValue ml_value_error (MlErrorKind kind, uint64_t start, uint64_t len)
{
	MlValue v =
	{
		.kind        = ML_VALUE_ERROR,
		.exact       = ml_rat_error (ML_OK),
		.approx      = 0.0,
		.error       = kind,
		.error_start = start,
		.error_len   = len
	};

	return v;
}

MlValue ml_value_none (void)
{
	MlValue v = ml_value_error (ML_EVAL_NONE, 0, 0);

	v.kind = ML_VALUE_NONE;
	return v;
}

MlValue ml_value_exact (MlRat x)
{
	MlValue v = ml_value_error (ML_EVAL_NONE, 0, 0);

	if (ml_rat_ok (x))
	{
		v.kind  = ML_VALUE_EXACT;
		v.exact = x;
	}

	else
	{
		v = ml_value_error (ml_error_from_result (ml_rat_status (x)), 0, 0);
	}

	return v;
}

MlValue ml_value_approx (double x)
{
	MlValue v = ml_value_error (ML_EVAL_NONE, 0, 0);

	if (isfinite (x))
	{
		v.kind   = ML_VALUE_APPROX;
		v.approx = x;
	}

	else
	{
		v = ml_value_error (
			ml_choose (isnan (x), ML_EVAL_NOT_REAL, ML_EVAL_TOO_LARGE),
			0,
			0
		);
	}

	return v;
}

MlValue ml_value_from_i64 (MlArena *arena, int64_t n)
{
	return ml_value_exact (ml_rat_from_i64 (arena, n));
}

MlValue ml_value_locate (MlValue v, uint64_t start, uint64_t len)
{
	MlValue located = v;

	if (v.kind == ML_VALUE_ERROR && v.error_len == 0)
	{
		located.error_start = start;
		located.error_len   = len;
	}

	return located;
}

int32_t ml_value_is_error (MlValue v)
{
	return v.kind == ML_VALUE_ERROR;
}

int32_t ml_value_is_number (MlValue v)
{
	return v.kind == ML_VALUE_EXACT || v.kind == ML_VALUE_APPROX;
}

int32_t ml_value_is_exact (MlValue v)
{
	return v.kind == ML_VALUE_EXACT;
}

MlI64Result ml_value_to_i64 (MlValue v)
{
	MlI64Result result =
	{
		.ok    = 0,
		.value = 0
	};

	if (v.kind == ML_VALUE_EXACT && ml_rat_is_integer (v.exact))
	{
		MlBig       magnitude = v.exact.num;
		int32_t     neg       = v.exact.num.negative;
		MlU64Result mag;

		magnitude.negative = 0;
		mag                = ml_big_to_u64 (magnitude);
		result.ok          = (
			   mag.ok
			&& ml_choose (
				neg,
				mag.value <= (uint64_t)INT64_MAX + 1,
				mag.value <= (uint64_t)INT64_MAX
			)
		);

		if (result.ok)
		{
			result.value = ml_choose (
				neg,
				(int64_t)(0 - mag.value),
				(int64_t)mag.value
			);
		}
	}

	return result;
}

double ml_value_to_double (MlArena *arena, MlValue v)
{
	return ml_choose (
		v.kind == ML_VALUE_EXACT,
		ml_rat_to_double (arena, v.exact),
		ml_choose (v.kind == ML_VALUE_APPROX, v.approx, 0.0)
	);
}

int32_t ml_value_cmp (MlArena *arena, MlValue a, MlValue b)
{
	double  x = ml_value_to_double (arena, a);
	double  y = ml_value_to_double (arena, b);
	int32_t order;

	if (a.kind == ML_VALUE_EXACT && b.kind == ML_VALUE_EXACT)
	{
		order = ml_rat_cmp (arena, a.exact, b.exact);
	}

	else
	{
		order = ml_choose (x < y, -1, ml_choose (x > y, 1, 0));
	}

	return order;
}

/* The first error among two operands, or a value that says there is none. */
static MlValue first_error (MlValue a, MlValue b)
{
	MlValue found = ml_value_none ();

	if (a.kind == ML_VALUE_ERROR)
	{
		found = a;
	}

	else if (b.kind == ML_VALUE_ERROR)
	{
		found = b;
	}

	else if (!ml_value_is_number (a) || !ml_value_is_number (b))
	{
		found = ml_value_error (ML_EVAL_SYNTAX, 0, 0);
	}

	return found;
}

MlValue ml_value_neg (MlArena *arena, MlValue a)
{
	MlValue result = a;

	if (a.kind == ML_VALUE_EXACT)
	{
		result = ml_value_exact (ml_rat_neg (arena, a.exact));
	}

	else if (a.kind == ML_VALUE_APPROX)
	{
		result = ml_value_approx (-a.approx);
	}

	else if (a.kind != ML_VALUE_ERROR)
	{
		result = ml_value_error (ML_EVAL_SYNTAX, 0, 0);
	}

	return result;
}

MlValue ml_value_add (MlArena *arena, MlValue a, MlValue b)
{
	MlValue result = first_error (a, b);

	if (
		   result.kind == ML_VALUE_NONE
		&& a.kind == ML_VALUE_EXACT
		&& b.kind == ML_VALUE_EXACT
	)
	{
		result = ml_value_exact (ml_rat_add (arena, a.exact, b.exact));
	}

	else if (result.kind == ML_VALUE_NONE)
	{
		result = ml_value_approx (
			ml_value_to_double (arena, a) + ml_value_to_double (arena, b)
		);
	}

	return result;
}

MlValue ml_value_sub (MlArena *arena, MlValue a, MlValue b)
{
	return ml_value_add (arena, a, ml_value_neg (arena, b));
}

MlValue ml_value_mul (MlArena *arena, MlValue a, MlValue b)
{
	MlValue result = first_error (a, b);

	if (
		   result.kind == ML_VALUE_NONE
		&& a.kind == ML_VALUE_EXACT
		&& b.kind == ML_VALUE_EXACT
	)
	{
		result = ml_value_exact (ml_rat_mul (arena, a.exact, b.exact));
	}

	else if (result.kind == ML_VALUE_NONE)
	{
		result = ml_value_approx (
			ml_value_to_double (arena, a) * ml_value_to_double (arena, b)
		);
	}

	return result;
}

MlValue ml_value_div (MlArena *arena, MlValue a, MlValue b)
{
	MlValue result = first_error (a, b);

	if (
		   result.kind == ML_VALUE_NONE
		&& a.kind == ML_VALUE_EXACT
		&& b.kind == ML_VALUE_EXACT
	)
	{
		result = ml_value_exact (ml_rat_div (arena, a.exact, b.exact));
	}

	else if (result.kind == ML_VALUE_NONE)
	{
		double divisor = ml_value_to_double (arena, b);

		result = ml_choose (
			divisor == 0.0,
			ml_value_error (ML_EVAL_DIV_ZERO, 0, 0),
			ml_value_approx (ml_value_to_double (arena, a) / divisor)
		);
	}

	return result;
}

/* a^b through doubles, for everything that has no exact answer. */
static MlValue pow_approx (MlArena *arena, MlValue a, MlValue b)
{
	double  x      = ml_value_to_double (arena, a);
	double  y      = ml_value_to_double (arena, b);
	int32_t whole  = y == floor (y);
	MlValue result = ml_value_none ();

	if (x == 0.0 && y == 0.0)
	{
		result = ml_value_error (ML_EVAL_ZERO_POW_ZERO, 0, 0);
	}

	else if (x == 0.0 && y < 0.0)
	{
		result = ml_value_error (ML_EVAL_DIV_ZERO, 0, 0);
	}

	else if (x < 0.0 && !whole)
	{
		result = ml_value_error (ML_EVAL_NOT_REAL, 0, 0);
	}

	else
	{
		result = ml_value_approx (pow (x, y));
	}

	return result;
}

/*
 * An exact base to an exact power: exact when the power is an integer or 1/2.
 */
static MlValue pow_exact (MlArena *arena, MlValue a, MlValue b)
{
	MlI64Result exponent = ml_value_to_i64 (b);
	MlValue     result   = ml_value_none ();
	MlRat       half     = ml_rat_make (
		arena,
		ml_big_from_u64 (arena, 1),
		ml_big_from_u64 (arena, 2)
	);

	if (ml_rat_is_zero (a.exact) && ml_rat_is_zero (b.exact))
	{
		result = ml_value_error (ML_EVAL_ZERO_POW_ZERO, 0, 0);
	}

	else if (ml_rat_is_zero (a.exact) && ml_rat_sign (b.exact) < 0)
	{
		result = ml_value_error (ML_EVAL_DIV_ZERO, 0, 0);
	}

	else if (ml_rat_is_zero (a.exact))
	{
		result = ml_value_exact (a.exact);
	}

	else if (exponent.ok)
	{
		result = ml_value_exact (ml_rat_pow (arena, a.exact, exponent.value));
	}

	else if (ml_rat_is_integer (b.exact))
	{
		/* a whole power that does not fit 64 bits: only 1 and -1 stay small */
		result = ml_value_error (ML_EVAL_TOO_LARGE, 0, 0);
	}

	else if (ml_rat_cmp (arena, b.exact, half) == 0)
	{
		MlRatSqrt root = ml_rat_sqrt_exact (arena, a.exact);

		result = ml_choose (
			root.exact,
			ml_value_exact (root.value),
			ml_choose (
				ml_rat_sign (a.exact) < 0,
				ml_value_error (ML_EVAL_NOT_REAL, 0, 0),
				pow_approx (arena, a, b)
			)
		);
	}

	else
	{
		result = pow_approx (arena, a, b);
	}

	return result;
}

MlValue ml_value_pow (MlArena *arena, MlValue a, MlValue b)
{
	MlValue result = first_error (a, b);

	if (
		   result.kind == ML_VALUE_NONE
		&& a.kind == ML_VALUE_EXACT
		&& b.kind == ML_VALUE_EXACT
	)
	{
		result = pow_exact (arena, a, b);
	}

	else if (result.kind == ML_VALUE_NONE)
	{
		result = pow_approx (arena, a, b);
	}

	return result;
}

MlResult ml_value_format (MlStr *out, MlArena *arena, MlValue v)
{
	MlResult r = ML_ERR_INVALID;

	(void)arena;

	if (v.kind == ML_VALUE_EXACT)
	{
		r = ml_rat_format (out, v.exact);
	}

	else if (v.kind == ML_VALUE_APPROX)
	{
		/* U+2248 ALMOST EQUAL TO */
		r = ml_str_appendf (
			out,
			"\xE2\x89\x88 %.*g",
			APPROX_DIGITS,
			v.approx
		);
	}

	return r;
}

/*
 * A range that is a plain name. The range of an error inside a function body
 * is the call, which has a parenthesis; quoting that as a name would be wrong.
 */
static int32_t is_word (MlSlice text)
{
	int32_t word = text.len > 0;

	for (uint64_t i = 0; i < text.len; i++)
	{
		if (text.data [i] == '(')
		{
			word = 0;
		}
	}

	return word;
}

MlResult ml_error_message (MlStr *out, MlValue v, MlSlice source)
{
	MlResult r = ml_str_append (out, ml_slice_cstr (ml_error_text (v.error)));

	if (
		   r == ML_OK
		&& v.error == ML_EVAL_UNDEFINED_NAME
		&& v.error_len > 0
		&& v.error_start + v.error_len <= source.len
		&& is_word (ml_slice_sub (source, v.error_start, v.error_len))
	)
	{
		r = ml_str_append (out, ml_slice_cstr (" '"));

		if (r == ML_OK)
		{
			r = ml_str_append (
				out,
				ml_slice_sub (source, v.error_start, v.error_len)
			);
		}

		if (r == ML_OK)
		{
			r = ml_str_append (out, ml_slice_cstr ("'"));
		}
	}

	return r;
}
