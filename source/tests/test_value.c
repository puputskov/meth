#include "test.h"

#include <matlog/math/value.h>

#include <math.h>
#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

static MlValue int_value (MlArena *a, int64_t n)
{
	return ml_value_from_i64 (a, n);
}

/* num / den as an exact value. */
static MlValue frac (MlArena *a, int64_t n, int64_t d)
{
	return ml_value_exact (
		ml_rat_make (
			a,
			ml_big_from_i64 (a, n),
			ml_big_from_i64 (a, d)
		)
	);
}

static int32_t is_text (MlArena *a, MlValue v, const char *expected)
{
	MlStr   s;
	int32_t same;

	ml_str_init (&s);
	same = (
		   ml_value_format (&s, a, v) == ML_OK
		&& ml_slice_equal (ml_str_view (s), lit (expected))
	);

	if (!same)
	{
		printf (
			"  value is \"%s\" (kind %d, error %d), expected \"%s\"\n",
			ml_str_cstr (s),
			(int32_t)v.kind,
			(int32_t)v.error,
			expected
		);
	}

	ml_str_free (&s);
	return same;
}

static int32_t is_error (MlValue v, MlErrorKind kind)
{
	return v.kind == ML_VALUE_ERROR && v.error == kind;
}

static void test_construct (void)
{
	MlArena a;

	ml_arena_init (&a, 0);

	CHECK (ml_value_none       ().kind == ML_VALUE_NONE);
	CHECK (!ml_value_is_number (ml_value_none ()));
	CHECK (!ml_value_is_error  (ml_value_none ()));
	CHECK (ml_value_is_number  (int_value (&a, 3)));
	CHECK (ml_value_is_exact   (int_value (&a, 3)));
	CHECK (ml_value_is_number  (ml_value_approx (1.5)));
	CHECK (!ml_value_is_exact  (ml_value_approx (1.5)));

	/* an approximation must be a finite number */
	CHECK (is_error (ml_value_approx (INFINITY), ML_EVAL_TOO_LARGE));
	CHECK (is_error (ml_value_approx (-INFINITY), ML_EVAL_TOO_LARGE));
	CHECK (is_error (ml_value_approx (NAN), ML_EVAL_NOT_REAL));

	/* a failed rational becomes an error of the matching kind */
	CHECK (is_error (
		ml_value_exact (ml_rat_error (ML_ERR_DIVZERO)),
		ML_EVAL_DIV_ZERO
	));
	CHECK (is_error (
		ml_value_exact (ml_rat_error (ML_ERR_LIMIT)),
		ML_EVAL_TOO_LARGE
	));
	CHECK (is_error (
		ml_value_exact (ml_rat_error (ML_ERR_NOMEM)),
		ML_EVAL_NO_MEMORY
	));
	CHECK (is_error (
		ml_value_exact (ml_rat_error (ML_ERR_PARSE)),
		ML_EVAL_SYNTAX
	));
	CHECK (ml_error_from_result (ML_ERR_RANGE) == ML_EVAL_DOMAIN);

	ml_arena_free (&a);
}

static void test_locate (void)
{
	MlValue e = ml_value_error (ML_EVAL_UNDEFINED_NAME, 0, 0);
	MlValue placed;

	/* an error without a position takes the one it is given */
	placed = ml_value_locate (e, 3, 4);
	CHECK (placed.error_start == 3 && placed.error_len == 4);

	/* an error that already has one keeps it */
	placed = ml_value_locate (placed, 9, 1);
	CHECK (placed.error_start == 3 && placed.error_len == 4);

	/* numbers are not touched */
	{
		MlArena a;
		MlValue v;

		ml_arena_init (&a, 0);
		v = ml_value_locate (int_value (&a, 5), 1, 2);
		CHECK (v.kind == ML_VALUE_EXACT && v.error_len == 0);
		ml_arena_free (&a);
	}
}

static void test_exact_arithmetic (void)
{
	MlArena a;

	ml_arena_init (&a, 0);

	CHECK (
		is_text (&a, ml_value_add (&a, frac (&a, 1, 3), frac (&a, 1, 6)), "1/2")
	);
	CHECK (
		is_text (
			&a,
			ml_value_sub (&a, frac (&a, 1, 2), frac (&a, 3, 4)),
			"-1/4"
		)
	);
	CHECK (
		is_text (&a, ml_value_mul (&a, frac (&a, 2, 3), frac (&a, 3, 4)), "1/2")
	);
	CHECK (
		is_text (&a, ml_value_div (&a, frac (&a, 1, 2), frac (&a, 1, 4)), "2")
	);
	CHECK (is_text (&a, ml_value_neg (&a, frac (&a, 1, 2)), "-1/2"));
	CHECK (is_text (&a, int_value (&a, -3), "-3"));
	CHECK (is_text (&a, int_value (&a, 0), "0"));

	/* the results are exact: no digits are lost */
	CHECK (
		ml_value_is_exact (ml_value_add (&a, frac (&a, 1, 3), frac (&a, 1, 3)))
	);
	CHECK (is_text (
		&a,
		ml_value_mul (
			&a,
			int_value (&a, 99999999999),
			int_value (&a, 99999999999)
		),
		"9999999999800000000001"
	));

	CHECK (is_error (
		ml_value_div (&a, int_value (&a, 1), int_value (&a, 0)),
		ML_EVAL_DIV_ZERO
	));

	ml_arena_free (&a);
}

static void test_approximation_spreads (void)
{
	MlArena a;
	MlValue root2;

	ml_arena_init (&a, 0);
	root2 = ml_value_approx (sqrt (2.0));

	/* approx op exact and exact op approx are approximate */
	CHECK (!ml_value_is_exact (ml_value_add (&a, root2, int_value (&a, 1))));
	CHECK (!ml_value_is_exact (ml_value_add (&a, int_value (&a, 1), root2)));
	CHECK (!ml_value_is_exact (ml_value_sub (&a, int_value (&a, 1), root2)));
	CHECK (!ml_value_is_exact (ml_value_mul (&a, root2, frac (&a, 1, 2))));
	CHECK (!ml_value_is_exact (ml_value_div (&a, int_value (&a, 1), root2)));
	CHECK (!ml_value_is_exact (ml_value_neg (&a, root2)));

	CHECK (is_text (&a, root2, "\xE2\x89\x88 1.41421356"));
	CHECK (is_text (
		&a,
		ml_value_add (&a, root2, int_value (&a, 1)),
		"\xE2\x89\x88 2.41421356"
	));
	CHECK (is_text (
		&a,
		ml_value_mul (&a, root2, root2),
		"\xE2\x89\x88 2"
	));
	CHECK (is_text (&a, ml_value_neg (&a, root2), "\xE2\x89\x88 -1.41421356"));
	CHECK (is_text (&a, ml_value_approx (1e30), "\xE2\x89\x88 1e+30"));
	CHECK (is_text (&a, ml_value_approx (0.125), "\xE2\x89\x88 0.125"));

	/* division by an approximate zero, and results that leave the range */
	CHECK (is_error (
		ml_value_div (&a, root2, ml_value_approx (0.0)),
		ML_EVAL_DIV_ZERO
	));
	CHECK (is_error (
		ml_value_mul (&a, ml_value_approx (1e200), ml_value_approx (1e200)),
		ML_EVAL_TOO_LARGE
	));

	ml_arena_free (&a);
}

static void test_pow (void)
{
	MlArena a;

	ml_arena_init (&a, 0);

	/* integer powers of exact numbers are exact */
	CHECK (
		is_text (
			&a,
			ml_value_pow (&a, int_value (&a, 2), int_value (&a, 10)),
			"1024"
		)
	);
	CHECK (
		is_text (
			&a,
			ml_value_pow (&a, int_value (&a, 2), int_value (&a, -2)),
			"1/4"
		)
	);
	CHECK (
		is_text (
			&a,
			ml_value_pow (&a, frac (&a, 2, 3), int_value (&a, 3)),
			"8/27"
		)
	);
	CHECK (
		is_text (
			&a,
			ml_value_pow (&a, int_value (&a, -3), int_value (&a, 3)),
			"-27"
		)
	);
	CHECK (
		is_text (
			&a,
			ml_value_pow (&a, int_value (&a, 5), int_value (&a, 0)),
			"1"
		)
	);
	CHECK (is_text (
		&a,
		ml_value_pow (&a, int_value (&a, 2), int_value (&a, 100)),
		"1267650600228229401496703205376"
	));

	/* the square root of a perfect square is exact, other roots are not */
	CHECK (
		is_text (&a, ml_value_pow (&a, int_value (&a, 4), frac (&a, 1, 2)), "2")
	);
	CHECK (
		is_text (&a, ml_value_pow (&a, frac (&a, 9, 4), frac (&a, 1, 2)), "3/2")
	);
	CHECK (is_text (
		&a,
		ml_value_pow (&a, int_value (&a, 2), frac (&a, 1, 2)),
		"\xE2\x89\x88 1.41421356"
	));
	CHECK (is_text (
		&a,
		ml_value_pow (&a, int_value (&a, 8), frac (&a, 1, 3)),
		"\xE2\x89\x88 2"
	));
	CHECK (is_text (
		&a,
		ml_value_pow (&a, int_value (&a, 2), frac (&a, -1, 2)),
		"\xE2\x89\x88 0.707106781"
	));

	/* zero */
	CHECK (is_error (
		ml_value_pow (&a, int_value (&a, 0), int_value (&a, 0)),
		ML_EVAL_ZERO_POW_ZERO
	));
	CHECK (is_error (
		ml_value_pow (&a, int_value (&a, 0), int_value (&a, -1)),
		ML_EVAL_DIV_ZERO
	));
	CHECK (
		is_text (
			&a,
			ml_value_pow (&a, int_value (&a, 0), int_value (&a, 3)),
			"0"
		)
	);
	CHECK (
		is_text (&a, ml_value_pow (&a, int_value (&a, 0), frac (&a, 1, 2)), "0")
	);
	CHECK (is_error (
		ml_value_pow (&a, ml_value_approx (0.0), ml_value_approx (0.0)),
		ML_EVAL_ZERO_POW_ZERO
	));

	/* a negative number to a fraction is complex */
	CHECK (
		ml_value_pow (&a, int_value (&a, -4), frac (&a, 1, 2)).kind
		== ML_VALUE_COMPLEX
	);
	CHECK (
		ml_value_pow (&a, int_value (&a, -8), frac (&a, 1, 3)).kind
		== ML_VALUE_COMPLEX
	);
	CHECK (
		ml_value_pow (&a, ml_value_approx (-2.0), ml_value_approx (0.5)).kind
		== ML_VALUE_COMPLEX
	);

	/* approximate operands */
	CHECK (is_text (
		&a,
		ml_value_pow (&a, ml_value_approx (2.0), int_value (&a, 3)),
		"\xE2\x89\x88 8"
	));
	CHECK (is_text (
		&a,
		ml_value_pow (&a, ml_value_approx (-2.0), ml_value_approx (3.0)),
		"\xE2\x89\x88 -8"
	));

	/* too large */
	CHECK (is_error (
		ml_value_pow (&a, int_value (&a, 2), int_value (&a, 1000000)),
		ML_EVAL_TOO_LARGE
	));
	CHECK (is_error (
		ml_value_pow (
			&a,
			int_value (&a, 2),
			ml_value_exact (
				ml_rat_make (
					&a,
					ml_big_parse (&a, lit ("100000000000000000000")),
					ml_big_from_u64 (&a, 1)
				)
			)
		),
		ML_EVAL_TOO_LARGE
	));
	CHECK (is_error (
		ml_value_pow (&a, ml_value_approx (10.0), int_value (&a, 400)),
		ML_EVAL_TOO_LARGE
	));

	ml_arena_free (&a);
}

static void test_compare_and_convert (void)
{
	MlArena     a;
	MlI64Result i;

	ml_arena_init (&a, 0);

	CHECK (ml_value_cmp (&a, frac (&a, 1, 3), frac (&a, 1, 2)) == -1);
	CHECK (ml_value_cmp (&a, frac (&a, 2, 4), frac (&a, 1, 2)) == 0);
	CHECK (ml_value_cmp (&a, int_value (&a, 2), int_value (&a, -2)) == 1);
	CHECK (ml_value_cmp (&a, ml_value_approx (1.5), int_value (&a, 2)) == -1);
	CHECK (ml_value_cmp (&a, int_value (&a, 2), ml_value_approx (1.5)) == 1);
	CHECK (
		ml_value_cmp (&a, ml_value_approx (1.5), ml_value_approx (1.5)) == 0
	);

	CHECK (ml_value_to_double (&a, frac (&a, 7, 2)) == 3.5);
	CHECK (ml_value_to_double (&a, ml_value_approx (2.25)) == 2.25);
	CHECK (ml_value_to_double (&a, ml_value_none ()) == 0.0);

	i = ml_value_to_i64 (int_value (&a, 42));
	CHECK (i.ok && i.value == 42);
	i = ml_value_to_i64 (int_value (&a, -42));
	CHECK (i.ok && i.value == -42);
	i = ml_value_to_i64 (int_value (&a, 0));
	CHECK (i.ok && i.value == 0);
	i = ml_value_to_i64 (int_value (&a, INT64_MAX));
	CHECK (i.ok && i.value == INT64_MAX);
	i = ml_value_to_i64 (int_value (&a, INT64_MIN));
	CHECK (i.ok && i.value == INT64_MIN);
	i = ml_value_to_i64 (frac (&a, 1, 2));
	CHECK (!i.ok);
	i = ml_value_to_i64 (ml_value_approx (3.0));
	CHECK (!i.ok);
	i = ml_value_to_i64 (ml_value_error (ML_EVAL_SYNTAX, 0, 0));
	CHECK (!i.ok);
	i = ml_value_to_i64 (
		ml_value_exact (
			ml_rat_make (
				&a,
				ml_big_parse (&a, lit ("9223372036854775808")),
				ml_big_from_u64 (&a, 1)
			)
		)
	);
	CHECK (!i.ok);
	i = ml_value_to_i64 (
		ml_value_exact (
			ml_rat_make (
				&a,
				ml_big_parse (&a, lit ("-9223372036854775809")),
				ml_big_from_u64 (&a, 1)
			)
		)
	);
	CHECK (!i.ok);

	ml_arena_free (&a);
}

static void test_errors_spread (void)
{
	MlArena a;
	MlValue e;
	MlValue one;

	ml_arena_init (&a, 0);
	e   = ml_value_error (ML_EVAL_UNDEFINED_NAME, 2, 3);
	one = int_value (&a, 1);

	/* the error comes back unchanged, with its position */
	CHECK (is_error     (ml_value_add (&a, e, one), ML_EVAL_UNDEFINED_NAME));
	CHECK (ml_value_add (&a, e, one).error_start == 2);
	CHECK (ml_value_add (&a, e, one).error_len == 3);
	CHECK (is_error     (ml_value_add (&a, one, e), ML_EVAL_UNDEFINED_NAME));
	CHECK (is_error     (ml_value_sub (&a, one, e), ML_EVAL_UNDEFINED_NAME));
	CHECK (is_error     (ml_value_mul (&a, e, one), ML_EVAL_UNDEFINED_NAME));
	CHECK (is_error     (ml_value_div (&a, one, e), ML_EVAL_UNDEFINED_NAME));
	CHECK (is_error     (ml_value_pow (&a, e, one), ML_EVAL_UNDEFINED_NAME));
	CHECK (is_error     (ml_value_pow (&a, one, e), ML_EVAL_UNDEFINED_NAME));
	CHECK (is_error     (ml_value_neg (&a, e), ML_EVAL_UNDEFINED_NAME));

	/* the first error wins */
	CHECK (is_error (
		ml_value_add (&a, ml_value_error (ML_EVAL_DEPTH, 0, 0), e),
		ML_EVAL_DEPTH
	));

	/* a value that is not a number is not an operand either */
	CHECK (is_error (ml_value_add (&a, ml_value_none (), one), ML_EVAL_SYNTAX));
	CHECK (is_error (ml_value_neg (&a, ml_value_none ()), ML_EVAL_SYNTAX));

	ml_arena_free (&a);
}

static void test_messages (void)
{
	MlArena a;
	MlStr   s;
	MlSlice source = lit ("1 + foo * 2");

	ml_arena_init (&a, 0);
	ml_str_init (&s);

	CHECK (ml_error_message (
		&s,
		ml_value_error (ML_EVAL_UNDEFINED_NAME, 4, 3),
		source
	) == ML_OK);
	CHECK (strcmp (ml_str_cstr (s), "undefined name 'foo'") == 0);

	ml_str_clear (&s);
	CHECK (ml_error_message (
		&s,
		ml_value_error (ML_EVAL_DIV_ZERO, 0, 0),
		source
	) == ML_OK);
	CHECK (strcmp (ml_str_cstr (s), "division by zero") == 0);

	/* a range outside the source, or none, adds no name */
	ml_str_clear (&s);
	CHECK (ml_error_message (
		&s,
		ml_value_error (ML_EVAL_UNDEFINED_NAME, 40, 3),
		source
	) == ML_OK);
	CHECK (strcmp (ml_str_cstr (s), "undefined name") == 0);

	/* every kind has a text */
	for (uint32_t k = 0; k < ML_EVAL_KIND_COUNT; k++)
	{
		CHECK (strlen (ml_error_text ((MlErrorKind)k)) > 0);
	}

	CHECK (strcmp (ml_error_text ((MlErrorKind)99), "unknown error") == 0);

	/* only numbers can be formatted */
	ml_str_clear (&s);
	CHECK (ml_value_format (&s, &a, ml_value_none ()) == ML_ERR_INVALID);
	CHECK (ml_value_format (
		&s,
		&a,
		ml_value_error (ML_EVAL_SYNTAX, 0, 0)
	) == ML_ERR_INVALID);

	ml_str_free (&s);
	ml_arena_free (&a);
}

static void test_matrix_value (void)
{
	MlArena a;
	MlMat   m;
	MlValue v;
	MlStr   s;

	ml_arena_init (&a, 0);
	ml_str_init (&s);
	m = ml_mat_identity (&a, 2);
	v = ml_value_matrix (m);

	CHECK (v.kind == ML_VALUE_MATRIX && ml_value_is_matrix (v));
	CHECK (!ml_value_is_number (v) && !ml_value_is_error (v));
	CHECK (ml_value_format (&s, &a, v) == ML_OK);
	CHECK (strcmp (ml_str_cstr (s), "[[1, 0], [0, 1]]") == 0);
	CHECK (!ml_value_is_matrix (ml_value_from_i64 (&a, 1)));

	/* a failed matrix becomes the matching error */
	CHECK (
		is_error (ml_value_matrix (ml_mat_error (ML_ERR_INVALID)), ML_EVAL_SIZE)
	);
	CHECK (
		is_error (
			ml_value_matrix (ml_mat_error (ML_ERR_DIVZERO)),
			ML_EVAL_SINGULAR
		)
	);
	CHECK (
		is_error (
			ml_value_matrix (ml_mat_error (ML_ERR_LIMIT)),
			ML_EVAL_TOO_LARGE
		)
	);
	CHECK (
		is_error (
			ml_value_matrix (ml_mat_error (ML_ERR_NOMEM)),
			ML_EVAL_NO_MEMORY
		)
	);
	CHECK (
		strcmp (ml_error_text (ML_EVAL_SIZE), "matrix sizes do not fit") == 0
	);
	CHECK (
		strcmp (ml_error_text (ML_EVAL_SINGULAR), "matrix is singular") == 0
	);

	ml_str_free (&s);
	ml_arena_free (&a);
}

void run_value_tests (void)
{
	test_construct ();
	test_locate ();
	test_exact_arithmetic ();
	test_approximation_spreads ();
	test_pow ();
	test_compare_and_convert ();
	test_errors_spread ();
	test_messages ();
	test_matrix_value ();
}
