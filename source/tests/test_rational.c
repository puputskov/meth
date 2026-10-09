#include "test.h"

#include <matlog/math/rational.h>

#include <math.h>
#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

/* num / den from decimal integer strings. */
static MlRat frac (MlArena *a, const char *n, const char *d)
{
	return ml_rat_make (
		a,
		ml_big_parse (a, lit (n)),
		ml_big_parse (a, lit (d))
	);
}

static int32_t is_text (MlRat x, const char *expected)
{
	MlStr   s;
	int32_t same;

	ml_str_init (&s);
	same = (
		   ml_rat_format (&s, x) == ML_OK
		&& ml_slice_equal (ml_str_view (s), lit (expected))
	);

	if (!same)
	{
		printf (
			"  value is \"%s\" (status %d), expected \"%s\"\n",
			ml_str_cstr (s),
			(int32_t)ml_rat_status (x),
			expected
		);
	}

	ml_str_free (&s);
	return same;
}

static int32_t decimal_is (
	MlArena    *a,
	MlRat      x,
	uint64_t   digits,
	const char *expected,
	int32_t    exact
)
{
	MlStr        s;
	MlRatDecimal d;
	int32_t      same;

	ml_str_init (&s);
	d    = ml_rat_format_decimal (&s, a, x, digits);
	same = (
		   d.result == ML_OK
		&& d.exact == exact
		&& ml_slice_equal (ml_str_view (s), lit (expected))
	);

	if (!same)
	{
		printf (
			"  decimal \"%s\" (exact %d), expected \"%s\" (exact %d)\n",
			ml_str_cstr (s),
			d.exact,
			expected,
			exact
		);
	}

	ml_str_free (&s);
	return same;
}

static void test_make (void)
{
	MlArena a;

	ml_arena_init (&a, 0);

	CHECK (is_text (frac (&a, "2", "4"), "1/2"));
	CHECK (is_text (frac (&a, "-2", "-4"), "1/2"));
	CHECK (is_text (frac (&a, "2", "-4"), "-1/2"));
	CHECK (is_text (frac (&a, "-2", "4"), "-1/2"));
	CHECK (is_text (frac (&a, "6", "3"), "2"));
	CHECK (is_text (frac (&a, "0", "5"), "0"));
	CHECK (is_text (frac (&a, "0", "-5"), "0"));
	CHECK (is_text (frac (&a, "7", "1"), "7"));
	CHECK (is_text (
		frac (&a, "100000000000000000000", "300000000000000000000"),
		"1/3"
	));
	CHECK (frac       (&a, "1", "0").num.status == ML_ERR_DIVZERO);
	CHECK (frac       (&a, "0", "0").num.status == ML_ERR_DIVZERO);
	CHECK (!ml_rat_ok (frac (&a, "1", "0")));

	CHECK (is_text (ml_rat_from_i64 (&a, -7), "-7"));
	CHECK (is_text (ml_rat_from_i64 (&a, 0), "0"));
	CHECK (is_text (ml_rat_zero (&a), "0"));
	CHECK (is_text (ml_rat_from_big (&a, ml_big_from_u64 (&a, 42)), "42"));

	CHECK (ml_rat_is_zero     (frac (&a, "0", "9")));
	CHECK (!ml_rat_is_zero    (frac (&a, "1", "9")));
	CHECK (ml_rat_is_integer  (frac (&a, "9", "3")));
	CHECK (!ml_rat_is_integer (frac (&a, "9", "4")));
	CHECK (ml_rat_sign        (frac (&a, "-1", "2")) == -1);
	CHECK (ml_rat_sign        (frac (&a, "1", "2")) == 1);
	CHECK (ml_rat_sign        (frac (&a, "0", "2")) == 0);

	ml_arena_free (&a);
}

static void test_parse_decimal (void)
{
	MlArena a;

	ml_arena_init (&a, 0);

	CHECK (is_text (ml_rat_parse_decimal (&a, lit ("0.75")), "3/4"));
	CHECK (is_text (ml_rat_parse_decimal (&a, lit ("-1.5")), "-3/2"));
	CHECK (is_text (ml_rat_parse_decimal (&a, lit ("+2.5")), "5/2"));
	CHECK (is_text (ml_rat_parse_decimal (&a, lit ("12")), "12"));
	CHECK (is_text (ml_rat_parse_decimal (&a, lit ("0.0")), "0"));
	CHECK (is_text (ml_rat_parse_decimal (&a, lit (".5")), "1/2"));
	CHECK (is_text (ml_rat_parse_decimal (&a, lit ("007.50")), "15/2"));
	CHECK (is_text (ml_rat_parse_decimal (&a, lit ("-0")), "0"));
	CHECK (is_text (ml_rat_parse_decimal (&a, lit ("0.1")), "1/10"));
	CHECK (is_text (
		ml_rat_parse_decimal (&a, lit ("3.14159")),
		"314159/100000"
	));

	CHECK (ml_rat_parse_decimal (&a, lit ("")).num.status == ML_ERR_PARSE);
	CHECK (ml_rat_parse_decimal (&a, lit ("-")).num.status == ML_ERR_PARSE);
	CHECK (ml_rat_parse_decimal (&a, lit (".")).num.status == ML_ERR_PARSE);
	CHECK (ml_rat_parse_decimal (&a, lit ("5.")).num.status == ML_ERR_PARSE);
	CHECK (ml_rat_parse_decimal (&a, lit ("1.2.3")).num.status == ML_ERR_PARSE);
	CHECK (ml_rat_parse_decimal (&a, lit ("1e3")).num.status == ML_ERR_PARSE);
	CHECK (ml_rat_parse_decimal (&a, lit ("1,5")).num.status == ML_ERR_PARSE);
	CHECK (ml_rat_parse_decimal (&a, lit (" 1")).num.status == ML_ERR_PARSE);
	CHECK (ml_rat_parse_decimal (&a, lit ("--1")).num.status == ML_ERR_PARSE);

	ml_arena_free (&a);
}

static void test_arithmetic (void)
{
	MlArena a;
	MlRat   half;
	MlRat   third;

	ml_arena_init (&a, 0);
	half  = frac (&a, "1", "2");
	third = frac (&a, "1", "3");

	CHECK (is_text (ml_rat_add (&a, third, frac (&a, "1", "6")), "1/2"));
	CHECK (is_text (ml_rat_add (&a, half, half), "1"));
	CHECK (is_text (ml_rat_add (&a, half, third), "5/6"));
	CHECK (is_text (ml_rat_add (&a, half, ml_rat_neg (&a, half)), "0"));
	CHECK (is_text (ml_rat_sub (&a, half, frac (&a, "3", "4")), "-1/4"));
	CHECK (is_text (ml_rat_sub (&a, third, half), "-1/6"));
	CHECK (
		is_text (
			ml_rat_mul (&a, frac (&a, "2", "3"), frac (&a, "3", "4")),
			"1/2"
		)
	);
	CHECK (
		is_text (
			ml_rat_mul (&a, frac (&a, "-2", "3"), frac (&a, "3", "4")),
			"-1/2"
		)
	);
	CHECK (
		is_text (
			ml_rat_mul (&a, frac (&a, "-2", "3"), frac (&a, "-3", "4")),
			"1/2"
		)
	);
	CHECK (is_text (ml_rat_mul (&a, half, ml_rat_zero (&a)), "0"));
	CHECK (is_text (ml_rat_div (&a, half, frac (&a, "1", "4")), "2"));
	CHECK (
		is_text (
			ml_rat_div (&a, frac (&a, "-3", "5"), frac (&a, "2", "7")),
			"-21/10"
		)
	);
	CHECK (
		is_text (
			ml_rat_div (&a, frac (&a, "3", "5"), frac (&a, "-2", "7")),
			"-21/10"
		)
	);
	CHECK (is_text (ml_rat_neg (&a, half), "-1/2"));
	CHECK (is_text (ml_rat_neg (&a, ml_rat_neg (&a, half)), "1/2"));
	CHECK (is_text (ml_rat_neg (&a, ml_rat_zero (&a)), "0"));
	CHECK (is_text (ml_rat_abs (&a, frac (&a, "-3", "7")), "3/7"));
	CHECK (is_text (ml_rat_recip (&a, frac (&a, "-2", "3")), "-3/2"));
	CHECK (is_text (ml_rat_recip (&a, frac (&a, "2", "3")), "3/2"));
	CHECK (is_text (ml_rat_recip (&a, frac (&a, "5", "1")), "1/5"));

	/* a long sum stays exact: 1/1 + 1/2 + ... + 1/10 = 7381/2520 */
	{
		MlRat sum = ml_rat_zero (&a);

		for (int64_t i = 1; i <= 10; i++)
		{
			sum = ml_rat_add (
				&a,
				sum,
				ml_rat_recip (&a, ml_rat_from_i64 (&a, i))
			);
		}

		CHECK (is_text (sum, "7381/2520"));
	}

	/* division by zero */
	CHECK (
		ml_rat_div (&a, half, ml_rat_zero (&a)).num.status == ML_ERR_DIVZERO
	);
	CHECK (ml_rat_recip (&a, ml_rat_zero (&a)).num.status == ML_ERR_DIVZERO);

	ml_arena_free (&a);
}

static void test_compare (void)
{
	MlArena a;

	ml_arena_init (&a, 0);

	CHECK (ml_rat_cmp (&a, frac (&a, "1", "3"), frac (&a, "1", "2")) == -1);
	CHECK (ml_rat_cmp (&a, frac (&a, "1", "2"), frac (&a, "1", "3")) == 1);
	CHECK (ml_rat_cmp (&a, frac (&a, "2", "4"), frac (&a, "1", "2")) == 0);
	CHECK (ml_rat_cmp (&a, frac (&a, "-1", "2"), frac (&a, "-1", "3")) == -1);
	CHECK (ml_rat_cmp (&a, frac (&a, "-1", "2"), frac (&a, "1", "3")) == -1);
	CHECK (ml_rat_cmp (&a, ml_rat_zero (&a), frac (&a, "-1", "100")) == 1);
	CHECK (ml_rat_cmp (&a, ml_rat_zero (&a), ml_rat_zero (&a)) == 0);
	CHECK (
		ml_rat_cmp (
			&a,
			frac (&a, "999999999999", "1000000000000"),
			ml_rat_from_i64 (&a, 1)
		) == -1
	);

	ml_arena_free (&a);
}

static void test_pow (void)
{
	MlArena a;
	MlRat   two_thirds;

	ml_arena_init (&a, 0);
	two_thirds = frac (&a, "2", "3");

	CHECK (is_text (ml_rat_pow (&a, two_thirds, 3), "8/27"));
	CHECK (is_text (ml_rat_pow (&a, two_thirds, -2), "9/4"));
	CHECK (is_text (ml_rat_pow (&a, two_thirds, 0), "1"));
	CHECK (is_text (ml_rat_pow (&a, two_thirds, 1), "2/3"));
	CHECK (is_text (ml_rat_pow (&a, two_thirds, -1), "3/2"));
	CHECK (is_text (ml_rat_pow (&a, frac (&a, "-2", "3"), 3), "-8/27"));
	CHECK (is_text (ml_rat_pow (&a, frac (&a, "-2", "3"), 2), "4/9"));
	CHECK (is_text (ml_rat_pow (&a, frac (&a, "-2", "3"), -3), "-27/8"));
	CHECK (is_text (ml_rat_pow (&a, frac (&a, "-2", "3"), -2), "9/4"));
	CHECK (is_text (ml_rat_pow (&a, ml_rat_from_i64 (&a, 10), 3), "1000"));
	CHECK (is_text (ml_rat_pow (&a, ml_rat_from_i64 (&a, 2), -1), "1/2"));
	CHECK (is_text (
		ml_rat_pow (&a, frac (&a, "1", "2"), 100),
		"1/1267650600228229401496703205376"
	));
	CHECK (is_text    (ml_rat_pow (&a, ml_rat_zero (&a), 5), "0"));
	CHECK (is_text    (ml_rat_pow (&a, ml_rat_zero (&a), 0), "1"));
	CHECK (ml_rat_pow (&a, ml_rat_zero (&a), -1).num.status == ML_ERR_DIVZERO);
	CHECK (ml_rat_pow (&a, two_thirds, 1000000).num.status == ML_ERR_LIMIT);
	CHECK (is_text (ml_rat_pow (&a, ml_rat_from_i64 (&a, 1), INT64_MAX), "1"));
	CHECK (is_text (ml_rat_pow (&a, ml_rat_from_i64 (&a, -1), INT64_MIN), "1"));
	CHECK (
		is_text (ml_rat_pow (&a, ml_rat_from_i64 (&a, -1), INT64_MAX), "-1")
	);

	ml_arena_free (&a);
}

static void test_sqrt (void)
{
	MlArena   a;
	MlRatSqrt s;

	ml_arena_init (&a, 0);

	s = ml_rat_sqrt_exact (&a, frac (&a, "9", "4"));
	CHECK (s.exact && is_text (s.value, "3/2"));
	s = ml_rat_sqrt_exact (&a, ml_rat_from_i64 (&a, 4));
	CHECK (s.exact && is_text (s.value, "2"));
	s = ml_rat_sqrt_exact (&a, ml_rat_zero (&a));
	CHECK (s.exact && is_text (s.value, "0"));
	s = ml_rat_sqrt_exact (&a, ml_rat_from_i64 (&a, 1));
	CHECK (s.exact && is_text (s.value, "1"));
	s = ml_rat_sqrt_exact (&a, frac (&a, "49", "100"));
	CHECK (s.exact && is_text (s.value, "7/10"));
	s = ml_rat_sqrt_exact (&a, frac (&a, "1", "1000000000000000000000"));
	CHECK (!s.exact);
	s = ml_rat_sqrt_exact (&a, frac (&a, "1", "100000000000000000000"));
	CHECK (s.exact && is_text (s.value, "1/10000000000"));

	s = ml_rat_sqrt_exact (&a, ml_rat_from_i64 (&a, 2));
	CHECK (!s.exact);
	s = ml_rat_sqrt_exact (&a, frac (&a, "1", "2"));
	CHECK (!s.exact);
	s = ml_rat_sqrt_exact (&a, frac (&a, "4", "3"));
	CHECK (!s.exact);
	s = ml_rat_sqrt_exact (&a, ml_rat_from_i64 (&a, -4));
	CHECK (!s.exact);
	s = ml_rat_sqrt_exact (&a, ml_rat_error (ML_ERR_PARSE));
	CHECK (!s.exact);

	ml_arena_free (&a);
}

static void test_to_double (void)
{
	MlArena a;
	MlRat   big;

	ml_arena_init (&a, 0);

	CHECK (ml_rat_to_double (&a, frac (&a, "7", "2")) == 3.5);
	CHECK (ml_rat_to_double (&a, frac (&a, "-7", "2")) == -3.5);
	CHECK (ml_rat_to_double (&a, ml_rat_zero (&a)) == 0.0);
	CHECK (ml_rat_to_double (&a, ml_rat_from_i64 (&a, 12345)) == 12345.0);
	CHECK (
		fabs (ml_rat_to_double (&a, frac (&a, "1", "3")) - 1.0 / 3.0) < 1e-16
	);
	CHECK (
		fabs (ml_rat_to_double (&a, frac (&a, "22", "7")) - 22.0 / 7.0) < 1e-15
	);

	/* numerator and denominator far too big for a double, ratio fine */
	big = ml_rat_make (
		&a,
		ml_big_pow (&a, ml_big_from_u64 (&a, 10), 400),
		ml_big_mul (
			&a,
			ml_big_pow (&a, ml_big_from_u64 (&a, 10), 399),
			ml_big_from_u64 (&a, 4)
		)
	);
	CHECK (is_text (big, "5/2"));
	big = ml_rat_make (
		&a,
		ml_big_add (
			&a,
			ml_big_pow (&a, ml_big_from_u64 (&a, 10), 400),
			ml_big_from_u64 (&a, 1)
		),
		ml_big_add (
			&a,
			ml_big_pow (&a, ml_big_from_u64 (&a, 10), 399),
			ml_big_from_u64 (&a, 1)
		)
	);
	CHECK (fabs (ml_rat_to_double (&a, big) - 10.0) < 1e-12);
	CHECK (
		isinf (
			ml_rat_to_double (
				&a,
				ml_rat_pow (&a, ml_rat_from_i64 (&a, 10), 400)
			)
		)
	);
	CHECK (
		ml_rat_to_double (
			&a,
			ml_rat_pow (&a, ml_rat_from_i64 (&a, 10), -400)
		) == 0.0
	);

	ml_arena_free (&a);
}

static void test_format (void)
{
	MlArena a;
	MlStr   s;

	ml_arena_init (&a, 0);
	ml_str_init (&s);

	CHECK (is_text (frac (&a, "7", "2"), "7/2"));
	CHECK (is_text (frac (&a, "-5", "8"), "-5/8"));
	CHECK (is_text (frac (&a, "8", "2"), "4"));

	/* an error value cannot be formatted */
	CHECK (ml_rat_format (&s, ml_rat_error (ML_ERR_DIVZERO)) == ML_ERR_DIVZERO);

	/* terminating expansions are exact */
	CHECK (decimal_is (&a, frac (&a, "7", "2"), 10, "3.5", 1));
	CHECK (decimal_is (&a, frac (&a, "-5", "8"), 10, "-0.625", 1));
	CHECK (decimal_is (&a, frac (&a, "1", "8"), 10, "0.125", 1));
	CHECK (decimal_is (&a, frac (&a, "1", "1000"), 10, "0.001", 1));
	CHECK (decimal_is (&a, ml_rat_from_i64 (&a, 1), 10, "1", 1));
	CHECK (decimal_is (&a, ml_rat_zero (&a), 10, "0", 1));
	CHECK (decimal_is (&a, ml_rat_from_i64 (&a, -42), 10, "-42", 1));
	CHECK (decimal_is (
		&a,
		ml_rat_pow (&a, ml_rat_from_i64 (&a, 10), 20),
		5,
		"100000000000000000000",
		1
	));
	CHECK (decimal_is (&a, frac (&a, "5", "2"), 1, "2.5", 1));

	/* cut off expansions are truncated and not exact */
	CHECK (decimal_is (&a, frac (&a, "1", "3"), 10, "0.3333333333", 0));
	CHECK (decimal_is (&a, frac (&a, "2", "3"), 4, "0.6666", 0));
	CHECK (decimal_is (&a, frac (&a, "-1", "3"), 3, "-0.333", 0));
	CHECK (decimal_is (&a, frac (&a, "22", "7"), 5, "3.14285", 0));
	CHECK (decimal_is (&a, frac (&a, "100", "3"), 3, "33.333", 0));
	CHECK (decimal_is (&a, frac (&a, "1", "3"), 0, "0", 0));
	CHECK (decimal_is (&a, frac (&a, "7", "2"), 0, "3", 0));
	CHECK (decimal_is (&a, frac (&a, "1", "7"), 12, "0.142857142857", 0));
	CHECK (decimal_is (&a, frac (&a, "7", "2"), 1, "3.5", 1));
	CHECK (decimal_is (&a, frac (&a, "1", "16"), 3, "0.062", 0));

	{
		MlRatDecimal d = ml_rat_format_decimal (
			&s,
			&a,
			ml_rat_error (ML_ERR_LIMIT),
			5
		);

		CHECK (d.result == ML_ERR_LIMIT);
	}

	ml_str_free (&s);
	ml_arena_free (&a);
}

static void test_errors (void)
{
	MlArena a;
	MlRat   e;
	MlRat   one;

	ml_arena_init (&a, 0);
	e   = ml_rat_error (ML_ERR_PARSE);
	one = ml_rat_from_i64 (&a, 1);

	CHECK (ml_rat_status (e) == ML_ERR_PARSE);
	CHECK (ml_rat_add    (&a, e, one).num.status == ML_ERR_PARSE);
	CHECK (ml_rat_add    (&a, one, e).num.status == ML_ERR_PARSE);
	CHECK (ml_rat_sub    (&a, one, e).num.status == ML_ERR_PARSE);
	CHECK (ml_rat_mul    (&a, e, one).num.status == ML_ERR_PARSE);
	CHECK (ml_rat_mul    (&a, one, e).num.status == ML_ERR_PARSE);
	CHECK (ml_rat_div    (&a, e, one).num.status == ML_ERR_PARSE);
	CHECK (ml_rat_div    (&a, one, e).num.status == ML_ERR_PARSE);
	CHECK (ml_rat_recip  (&a, e).num.status == ML_ERR_PARSE);
	CHECK (ml_rat_pow    (&a, e, 2).num.status == ML_ERR_PARSE);

	ml_arena_free (&a);
}

void run_rational_tests (void)
{
	test_make ();
	test_parse_decimal ();
	test_arithmetic ();
	test_compare ();
	test_pow ();
	test_sqrt ();
	test_to_double ();
	test_format ();
	test_errors ();
}
