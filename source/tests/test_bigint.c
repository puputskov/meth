#include "test.h"

#include <matlog/core/bigint.h>

#include <math.h>
#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

/* Parses a decimal string (the test strings are valid). */
static MlBig num (MlArena *a, const char *text)
{
	return ml_big_parse (a, lit (text));
}

static int32_t is_text (MlBig x, const char *expected)
{
	MlStr   s;
	int32_t same;

	ml_str_init (&s);
	same = (
		   ml_big_format (&s, x) == ML_OK
		&& ml_slice_equal (ml_str_view (s), lit (expected))
	);

	if (!same)
	{
		printf (
			"  value is \"%s\" (status %d), expected \"%s\"\n",
			ml_str_cstr (s),
			(int32_t)x.status,
			expected
		);
	}

	ml_str_free (&s);
	return same;
}

static void test_construct_and_format (void)
{
	MlArena a;

	ml_arena_init (&a, 0);

	CHECK (is_text (ml_big_zero (), "0"));
	CHECK (is_text (ml_big_from_u64 (&a, 0), "0"));
	CHECK (is_text (ml_big_from_u64 (&a, 1), "1"));
	CHECK (is_text (ml_big_from_u64 (&a, 4294967295u), "4294967295"));
	CHECK (is_text (ml_big_from_u64 (&a, 4294967296u), "4294967296"));
	CHECK (is_text (
		ml_big_from_u64 (&a, 18446744073709551615u),
		"18446744073709551615"
	));
	CHECK (is_text (ml_big_from_i64 (&a, -1), "-1"));
	CHECK (is_text (ml_big_from_i64 (&a, 0), "0"));
	CHECK (is_text (
		ml_big_from_i64 (&a, INT64_MIN),
		"-9223372036854775808"
	));
	CHECK (is_text (
		ml_big_from_i64 (&a, INT64_MAX),
		"9223372036854775807"
	));

	/* parsing and printing are inverse for long numbers */
	CHECK (is_text (
		num (&a, "123456789012345678901234567890123456789"),
		"123456789012345678901234567890123456789"
	));
	CHECK (is_text (
		num (&a, "-1000000000000000000000000000000"),
		"-1000000000000000000000000000000"
	));
	CHECK (is_text (num (&a, "+42"), "42"));
	CHECK (is_text (num (&a, "-0"), "0"));
	CHECK (is_text (num (&a, "0000123"), "123"));
	CHECK (is_text (num (&a, "000000000000000000000"), "0"));
	CHECK (is_text (num (&a, "1000000000"), "1000000000"));
	CHECK (is_text (num (&a, "999999999"), "999999999"));
	CHECK (is_text (num (&a, "1000000000000000000"), "1000000000000000000"));

	/* bad input */
	CHECK (num (&a, "").status == ML_ERR_PARSE);
	CHECK (num (&a, "-").status == ML_ERR_PARSE);
	CHECK (num (&a, "+").status == ML_ERR_PARSE);
	CHECK (num (&a, "12a").status == ML_ERR_PARSE);
	CHECK (num (&a, " 1").status == ML_ERR_PARSE);
	CHECK (num (&a, "1 ").status == ML_ERR_PARSE);
	CHECK (num (&a, "1.5").status == ML_ERR_PARSE);
	CHECK (num (&a, "--1").status == ML_ERR_PARSE);

	/* an error value cannot be formatted */
	{
		MlStr s;

		ml_str_init (&s);
		CHECK (ml_big_format (&s, ml_big_error (ML_ERR_PARSE)) == ML_ERR_PARSE);
		ml_str_free (&s);
	}

	ml_arena_free (&a);
}

static void test_compare (void)
{
	MlArena a;
	MlBig   big;
	MlBig   neg_big;

	ml_arena_init (&a, 0);
	big     = num (&a, "100000000000000000000");
	neg_big = num (&a, "-100000000000000000000");

	CHECK (ml_big_cmp (num (&a, "5"), num (&a, "5")) == 0);
	CHECK (ml_big_cmp (num (&a, "5"), num (&a, "6")) == -1);
	CHECK (ml_big_cmp (num (&a, "6"), num (&a, "5")) == 1);
	CHECK (ml_big_cmp (num (&a, "-5"), num (&a, "5")) == -1);
	CHECK (ml_big_cmp (num (&a, "5"), num (&a, "-5")) == 1);
	CHECK (ml_big_cmp (num (&a, "-5"), num (&a, "-6")) == 1);
	CHECK (ml_big_cmp (num (&a, "-6"), num (&a, "-5")) == -1);
	CHECK (ml_big_cmp (big, num (&a, "99999999999999999999")) == 1);
	CHECK (ml_big_cmp (neg_big, num (&a, "-99999999999999999999")) == -1);
	CHECK (ml_big_cmp (big, neg_big) == 1);
	CHECK (ml_big_cmp (ml_big_zero (), num (&a, "-0")) == 0);
	CHECK (ml_big_cmp (ml_big_zero (), num (&a, "1")) == -1);

	CHECK (ml_big_is_zero  (ml_big_zero ()));
	CHECK (!ml_big_is_zero (num (&a, "1")));
	CHECK (ml_big_is_odd   (num (&a, "7")));
	CHECK (ml_big_is_odd   (num (&a, "-7")));
	CHECK (!ml_big_is_odd  (num (&a, "8")));
	CHECK (!ml_big_is_odd  (ml_big_zero ()));
	CHECK (!ml_big_is_odd  (num (&a, "4294967296")));
	CHECK (ml_big_is_odd   (num (&a, "4294967297")));

	ml_arena_free (&a);
}

static void test_add_sub (void)
{
	MlArena a;

	ml_arena_init (&a, 0);

	/* carries across limbs */
	CHECK (is_text (
		ml_big_add (&a, num (&a, "4294967295"), num (&a, "1")),
		"4294967296"
	));
	CHECK (is_text (
		ml_big_add (&a, num (&a, "18446744073709551615"), num (&a, "1")),
		"18446744073709551616"
	));
	CHECK (is_text (
		ml_big_add (
			&a,
			num (&a, "340282366920938463463374607431768211455"),
			num (&a, "1")
		),
		"340282366920938463463374607431768211456"
	));

	/* every sign combination */
	CHECK (is_text (ml_big_add (&a, num (&a, "7"), num (&a, "5")), "12"));
	CHECK (is_text (ml_big_add (&a, num (&a, "7"), num (&a, "-5")), "2"));
	CHECK (is_text (ml_big_add (&a, num (&a, "-7"), num (&a, "5")), "-2"));
	CHECK (is_text (ml_big_add (&a, num (&a, "-7"), num (&a, "-5")), "-12"));
	CHECK (is_text (ml_big_add (&a, num (&a, "5"), num (&a, "-7")), "-2"));
	CHECK (is_text (ml_big_add (&a, num (&a, "-5"), num (&a, "7")), "2"));
	CHECK (is_text (ml_big_add (&a, num (&a, "5"), num (&a, "-5")), "0"));

	/* borrow across limbs */
	CHECK (is_text (
		ml_big_sub (&a, num (&a, "4294967296"), num (&a, "1")),
		"4294967295"
	));
	CHECK (is_text (
		ml_big_sub (
			&a,
			num (&a, "340282366920938463463374607431768211456"),
			num (&a, "1")
		),
		"340282366920938463463374607431768211455"
	));
	CHECK (is_text (ml_big_sub (&a, num (&a, "5"), num (&a, "7")), "-2"));
	CHECK (is_text (ml_big_sub (&a, num (&a, "-5"), num (&a, "-7")), "2"));
	CHECK (is_text (ml_big_sub (&a, num (&a, "9"), num (&a, "9")), "0"));
	CHECK (is_text (ml_big_sub (&a, ml_big_zero (), num (&a, "9")), "-9"));
	CHECK (is_text (ml_big_add (&a, ml_big_zero (), ml_big_zero ()), "0"));

	CHECK (is_text (ml_big_neg (&a, num (&a, "12")), "-12"));
	CHECK (is_text (ml_big_neg (&a, num (&a, "-12")), "12"));
	CHECK (is_text (ml_big_neg (&a, ml_big_zero ()), "0"));
	CHECK (is_text (ml_big_abs (&a, num (&a, "-12")), "12"));
	CHECK (is_text (ml_big_abs (&a, num (&a, "12")), "12"));

	ml_arena_free (&a);
}

static void test_mul (void)
{
	MlArena a;
	MlBig   f;

	ml_arena_init (&a, 0);

	CHECK (is_text (ml_big_mul (&a, num (&a, "6"), num (&a, "7")), "42"));
	CHECK (is_text (ml_big_mul (&a, num (&a, "-6"), num (&a, "7")), "-42"));
	CHECK (is_text (ml_big_mul (&a, num (&a, "-6"), num (&a, "-7")), "42"));
	CHECK (is_text (ml_big_mul (&a, num (&a, "0"), num (&a, "-7")), "0"));
	CHECK (is_text (ml_big_mul (&a, num (&a, "12345"), ml_big_zero ()), "0"));
	CHECK (is_text (
		ml_big_mul (&a, num (&a, "99999999999"), num (&a, "99999999999")),
		"9999999999800000000001"
	));
	CHECK (is_text (
		ml_big_mul (
			&a,
			num (&a, "18446744073709551616"),
			num (&a, "18446744073709551616")
		),
		"340282366920938463463374607431768211456"
	));
	CHECK (is_text (
		ml_big_mul (&a, num (&a, "4294967295"), num (&a, "4294967295")),
		"18446744065119617025"
	));

	/* 25! and 100! by repeated multiplication */
	f = ml_big_from_u64 (&a, 1);

	for (uint64_t i = 2; i <= 25; i++)
	{
		f = ml_big_mul (&a, f, ml_big_from_u64 (&a, i));
	}

	CHECK (is_text (f, "15511210043330985984000000"));

	for (uint64_t i = 26; i <= 100; i++)
	{
		f = ml_big_mul (&a, f, ml_big_from_u64 (&a, i));
	}

	CHECK (is_text (
		f,
		"93326215443944152681699238856266700490715968264381621468592963895217"
		"5999932299156089414639761565182862536979208272237582511852109168640000"
		"00000000000000000000"
	));

	ml_arena_free (&a);
}

static void test_divmod (void)
{
	MlArena  a;
	MlBigDiv d;
	uint64_t seed        = 12345;
	uint32_t patterns [] = {
		0, 1, 2, 0x7FFFFFFFu, 0x80000000u, 0x80000001u, 0xFFFFFFFEu,
		0xFFFFFFFFu
	};

	int32_t all_good = 1;

	ml_arena_init (&a, 0);

	/* truncation toward zero, remainder with the sign of the dividend */
	d = ml_big_divmod (&a, num (&a, "17"), num (&a, "5"));
	CHECK (is_text (d.quot, "3") && is_text (d.rem, "2"));
	d = ml_big_divmod (&a, num (&a, "-17"), num (&a, "5"));
	CHECK (is_text (d.quot, "-3") && is_text (d.rem, "-2"));
	d = ml_big_divmod (&a, num (&a, "17"), num (&a, "-5"));
	CHECK (is_text (d.quot, "-3") && is_text (d.rem, "2"));
	d = ml_big_divmod (&a, num (&a, "-17"), num (&a, "-5"));
	CHECK (is_text (d.quot, "3") && is_text (d.rem, "-2"));
	d = ml_big_divmod (&a, num (&a, "4"), num (&a, "9"));
	CHECK (is_text (d.quot, "0") && is_text (d.rem, "4"));
	d = ml_big_divmod (&a, num (&a, "-4"), num (&a, "9"));
	CHECK (is_text (d.quot, "0") && is_text (d.rem, "-4"));
	d = ml_big_divmod (&a, num (&a, "9"), num (&a, "9"));
	CHECK (is_text (d.quot, "1") && is_text (d.rem, "0"));
	d = ml_big_divmod (&a, ml_big_zero (), num (&a, "9"));
	CHECK (is_text (d.quot, "0") && is_text (d.rem, "0"));

	/* big dividends, one limb and several limb divisors */
	d = ml_big_divmod (
		&a,
		num (&a, "1000000000000000000000000000000"),
		num (&a, "7")
	);
	CHECK (is_text (d.quot, "142857142857142857142857142857"));
	CHECK (is_text (d.rem, "1"));
	d = ml_big_divmod (
		&a,
		num (&a, "340282366920938463463374607431768211456"),
		num (&a, "18446744073709551616")
	);
	CHECK (is_text (d.quot, "18446744073709551616") && is_text (d.rem, "0"));
	d = ml_big_divmod (
		&a,
		num (&a, "340282366920938463463374607431768211457"),
		num (&a, "18446744073709551617")
	);
	CHECK (is_text (d.quot, "18446744073709551615"));
	CHECK (is_text (d.rem, "2"));

	/* division by zero */
	d = ml_big_divmod (&a, num (&a, "5"), ml_big_zero ());
	CHECK (d.quot.status == ML_ERR_DIVZERO && d.rem.status == ML_ERR_DIVZERO);
	d = ml_big_divmod (&a, ml_big_zero (), ml_big_zero ());
	CHECK (d.quot.status == ML_ERR_DIVZERO);

	/* limbs full of edge values: q * y + r == x and |r| < |y| */
	for (uint32_t trial = 0; trial < 3000 && all_good; trial++)
	{
		uint32_t xl [9];
		uint32_t yl [5];
		uint64_t xn = 1 + trial % 9;
		uint64_t yn = 1 + (trial / 9) % 5;
		MlBig    x;
		MlBig    y;
		MlBig    back;

		for (uint64_t i = 0; i < xn; i++)
		{
			seed   = seed * 6364136223846793005u + 1442695040888963407u;
			xl [i] = ml_choose (
				(seed >> 60) < 10,
				patterns [(seed >> 33) % 8],
				(uint32_t)(seed >> 32)
			);
		}

		for (uint64_t i = 0; i < yn; i++)
		{
			seed   = seed * 6364136223846793005u + 1442695040888963407u;
			yl [i] = ml_choose (
				(seed >> 60) < 10,
				patterns [(seed >> 33) % 8],
				(uint32_t)(seed >> 32)
			);
		}

		x = ml_big_zero ();
		y = ml_big_zero ();

		/* build the numbers from limbs with shifts and additions */
		for (uint64_t i = xn; i > 0; i--)
		{
			x = ml_big_add (
				&a,
				ml_big_shl (&a, x, 32),
				ml_big_from_u64 (&a, xl [i - 1])
			);
		}

		for (uint64_t i = yn; i > 0; i--)
		{
			y = ml_big_add (
				&a,
				ml_big_shl (&a, y, 32),
				ml_big_from_u64 (&a, yl [i - 1])
			);
		}

		if (!ml_big_is_zero (y))
		{
			d        = ml_big_divmod (&a, x, y);
			back     = ml_big_add (&a, ml_big_mul (&a, d.quot, y), d.rem);
			all_good = (
				   ml_big_cmp (back, x) == 0
				&& ml_big_cmp (d.rem, ml_big_zero ()) >= 0
				&& ml_big_cmp (d.rem, y) < 0
			);
		}
	}

	CHECK (all_good);

	ml_arena_free (&a);
}

static void test_gcd (void)
{
	MlArena a;

	ml_arena_init (&a, 0);

	CHECK (is_text (ml_big_gcd (&a, num (&a, "48"), num (&a, "18")), "6"));
	CHECK (is_text (ml_big_gcd (&a, num (&a, "-12"), num (&a, "18")), "6"));
	CHECK (is_text (ml_big_gcd (&a, num (&a, "12"), num (&a, "-18")), "6"));
	CHECK (is_text (ml_big_gcd (&a, ml_big_zero (), num (&a, "5")), "5"));
	CHECK (is_text (ml_big_gcd (&a, num (&a, "5"), ml_big_zero ()), "5"));
	CHECK (is_text (ml_big_gcd (&a, ml_big_zero (), ml_big_zero ()), "0"));
	CHECK (is_text (ml_big_gcd (&a, num (&a, "17"), num (&a, "13")), "1"));
	CHECK (is_text (
		ml_big_gcd (
			&a,
			ml_big_pow (&a, num (&a, "2"), 100),
			ml_big_pow (&a, num (&a, "6"), 50)
		),
		"1125899906842624"
	));
	CHECK (
		ml_big_gcd (
			&a,
			ml_big_error (ML_ERR_PARSE),
			num (&a, "5")
		).status == ML_ERR_PARSE
	);

	ml_arena_free (&a);
}

static void test_pow (void)
{
	MlArena a;

	ml_arena_init (&a, 0);

	CHECK (is_text (ml_big_pow (&a, num (&a, "2"), 10), "1024"));
	CHECK (is_text (
		ml_big_pow (&a, num (&a, "2"), 100),
		"1267650600228229401496703205376"
	));
	CHECK (is_text (ml_big_pow (&a, num (&a, "-3"), 3), "-27"));
	CHECK (is_text (ml_big_pow (&a, num (&a, "-3"), 4), "81"));
	CHECK (is_text (ml_big_pow (&a, num (&a, "5"), 0), "1"));
	CHECK (is_text (ml_big_pow (&a, ml_big_zero (), 0), "1"));
	CHECK (is_text (ml_big_pow (&a, ml_big_zero (), 5), "0"));
	CHECK (is_text (
		ml_big_pow (&a, num (&a, "10"), 30),
		"1000000000000000000000000000000"
	));

	/* trivial bases with huge exponents are fast and exact */
	CHECK (is_text (ml_big_pow (&a, num (&a, "1"), UINT64_MAX), "1"));
	CHECK (is_text (ml_big_pow (&a, ml_big_zero (), UINT64_MAX), "0"));
	CHECK (is_text (ml_big_pow (&a, num (&a, "-1"), UINT64_MAX), "-1"));
	CHECK (is_text (ml_big_pow (&a, num (&a, "-1"), UINT64_MAX - 1), "1"));

	/* too large */
	CHECK (ml_big_pow (&a, num (&a, "2"), 200000).status == ML_ERR_LIMIT);
	CHECK (ml_big_pow (&a, num (&a, "10"), UINT64_MAX).status == ML_ERR_LIMIT);
	CHECK (
		ml_big_pow (&a, ml_big_error (ML_ERR_PARSE), 2).status == ML_ERR_PARSE
	);

	/* the largest allowed value is still fine */
	CHECK (ml_big_ok (ml_big_pow (&a, num (&a, "2"), 100000)));
	CHECK (
		ml_big_bit_length (ml_big_pow (&a, num (&a, "2"), 100000)) == 100001
	);

	ml_arena_free (&a);
}

static void test_shift_and_bits (void)
{
	MlArena a;

	ml_arena_init (&a, 0);

	CHECK (is_text (ml_big_shl (&a, num (&a, "1"), 0), "1"));
	CHECK (is_text (ml_big_shl (&a, num (&a, "1"), 10), "1024"));
	CHECK (is_text (
		ml_big_shl (&a, num (&a, "1"), 100),
		"1267650600228229401496703205376"
	));
	CHECK (is_text (ml_big_shl (&a, num (&a, "-3"), 33), "-25769803776"));
	CHECK (is_text (ml_big_shl (&a, ml_big_zero (), 500), "0"));

	CHECK (is_text (ml_big_shr (&a, num (&a, "1024"), 10), "1"));
	CHECK (is_text (ml_big_shr (&a, num (&a, "1025"), 1), "512"));
	CHECK (is_text (
		ml_big_shr (&a, num (&a, "1267650600228229401496703205376"), 100),
		"1"
	));
	CHECK (is_text    (ml_big_shr (&a, num (&a, "-5"), 1), "-2"));
	CHECK (is_text    (ml_big_shr (&a, num (&a, "5"), 3), "0"));
	CHECK (is_text    (ml_big_shr (&a, num (&a, "-5"), 3), "0"));
	CHECK (is_text    (ml_big_shr (&a, num (&a, "5"), 1000), "0"));
	CHECK (is_text    (ml_big_shr (&a, num (&a, "8589934592"), 32), "2"));
	CHECK (ml_big_shl (&a, num (&a, "1"), 200000).status == ML_ERR_LIMIT);

	CHECK (ml_big_bit_length (ml_big_zero ()) == 0);
	CHECK (ml_big_bit_length (num (&a, "1")) == 1);
	CHECK (ml_big_bit_length (num (&a, "255")) == 8);
	CHECK (ml_big_bit_length (num (&a, "256")) == 9);
	CHECK (ml_big_bit_length (num (&a, "4294967295")) == 32);
	CHECK (ml_big_bit_length (num (&a, "4294967296")) == 33);
	CHECK (ml_big_bit_length (num (&a, "18446744073709551616")) == 65);
	CHECK (ml_big_bit_length (num (&a, "-255")) == 8);

	ml_arena_free (&a);
}

static void test_conversions (void)
{
	MlArena     a;
	MlU64Result u;
	double      d;

	ml_arena_init (&a, 0);

	u = ml_big_to_u64 (num (&a, "18446744073709551615"));
	CHECK (u.ok && u.value == UINT64_MAX);
	u = ml_big_to_u64 (num (&a, "18446744073709551616"));
	CHECK (!u.ok);
	u = ml_big_to_u64 (num (&a, "-1"));
	CHECK (!u.ok);
	u = ml_big_to_u64 (ml_big_zero ());
	CHECK (u.ok && u.value == 0);
	u = ml_big_to_u64 (num (&a, "4294967296"));
	CHECK (u.ok && u.value == 4294967296u);
	u = ml_big_to_u64 (ml_big_error (ML_ERR_LIMIT));
	CHECK (!u.ok);

	CHECK (ml_big_to_double (ml_big_zero ()) == 0.0);
	CHECK (ml_big_to_double (num (&a, "12345")) == 12345.0);
	CHECK (ml_big_to_double (num (&a, "-12345")) == -12345.0);
	CHECK (
		ml_big_to_double (
			num (&a, "18446744073709551616")
		) == 18446744073709551616.0
	);
	d = ml_big_to_double (num (&a, "1000000000000000000000000000000"));
	CHECK (fabs (d - 1e30) / 1e30 < 1e-15);
	d = ml_big_to_double (ml_big_pow (&a, num (&a, "2"), 2000));
	CHECK (isinf (d));
	d = ml_big_to_double (ml_big_pow (&a, num (&a, "2"), 1000));
	CHECK (fabs (d - ldexp (1.0, 1000)) == 0.0);

	ml_arena_free (&a);
}

static void test_isqrt (void)
{
	MlArena a;
	MlBig   x;
	MlBig   s;
	int32_t all_good = 1;

	ml_arena_init (&a, 0);

	CHECK (is_text (ml_big_isqrt (&a, ml_big_zero ()), "0"));
	CHECK (is_text (ml_big_isqrt (&a, num (&a, "1")), "1"));
	CHECK (is_text (ml_big_isqrt (&a, num (&a, "2")), "1"));
	CHECK (is_text (ml_big_isqrt (&a, num (&a, "3")), "1"));
	CHECK (is_text (ml_big_isqrt (&a, num (&a, "4")), "2"));
	CHECK (is_text (ml_big_isqrt (&a, num (&a, "15")), "3"));
	CHECK (is_text (ml_big_isqrt (&a, num (&a, "16")), "4"));
	CHECK (is_text (
		ml_big_isqrt (&a, num (&a, "100000000000000000000")),
		"10000000000"
	));
	CHECK (is_text (
		ml_big_isqrt (&a, num (&a, "99999999999999999999")),
		"9999999999"
	));
	CHECK (ml_big_isqrt (&a, num (&a, "-4")).status == ML_ERR_RANGE);

	/* floor (sqrt (x)) squared is at most x, the next one is above */
	x = ml_big_from_u64 (&a, 1);

	for (uint32_t i = 0; i < 300 && all_good; i++)
	{
		MlBig next;

		x = ml_big_add (
			&a,
			ml_big_mul (&a, x, num (&a, "7")),
			num (&a, "3")
		);
		s        = ml_big_isqrt (&a, x);
		next     = ml_big_add (&a, s, num (&a, "1"));
		all_good = (
			   ml_big_cmp (ml_big_mul (&a, s, s), x) <= 0
			&& ml_big_cmp (ml_big_mul (&a, next, next), x) > 0
		);
	}

	CHECK (all_good);

	ml_arena_free (&a);
}

static void test_errors (void)
{
	MlArena a;
	MlBig   e;
	MlBig   five;

	ml_arena_init (&a, 0);
	e    = ml_big_error (ML_ERR_DIVZERO);
	five = num (&a, "5");

	/* an error value spreads through every operation */
	CHECK (!ml_big_ok    (e));
	CHECK (ml_big_add    (&a, e, five).status == ML_ERR_DIVZERO);
	CHECK (ml_big_add    (&a, five, e).status == ML_ERR_DIVZERO);
	CHECK (ml_big_sub    (&a, five, e).status == ML_ERR_DIVZERO);
	CHECK (ml_big_mul    (&a, e, five).status == ML_ERR_DIVZERO);
	CHECK (ml_big_mul    (&a, five, e).status == ML_ERR_DIVZERO);
	CHECK (ml_big_divmod (&a, e, five).quot.status == ML_ERR_DIVZERO);
	CHECK (ml_big_divmod (&a, five, e).rem.status == ML_ERR_DIVZERO);
	CHECK (ml_big_shl    (&a, e, 1).status == ML_ERR_DIVZERO);
	CHECK (ml_big_shr    (&a, e, 1).status == ML_ERR_DIVZERO);
	CHECK (ml_big_isqrt  (&a, e).status == ML_ERR_DIVZERO);
	CHECK (ml_big_neg    (&a, e).status == ML_ERR_DIVZERO);

	/* the first error wins */
	CHECK (
		ml_big_add (&a, ml_big_error (ML_ERR_LIMIT), e).status
		== ML_ERR_LIMIT
	);

	/* products that are too large are caught before they are computed */
	{
		MlBig huge = ml_big_pow (&a, num (&a, "2"), 100000);

		CHECK (ml_big_ok  (huge));
		CHECK (ml_big_mul (&a, huge, huge).status == ML_ERR_LIMIT);
		CHECK (ml_big_ok  (ml_big_add (&a, huge, huge)));
		CHECK (
			ml_big_add (
				&a,
				ml_big_pow (&a, num (&a, "2"), 102399),
				ml_big_pow (&a, num (&a, "2"), 102399)
			).status == ML_ERR_LIMIT
		);
	}

	/* too many digits */
	{
		MlStr digits;

		ml_str_init (&digits);

		for (uint32_t i = 0; i < 31001; i++)
		{
			CHECK (ml_str_append (&digits, lit ("9")) == ML_OK);
		}

		CHECK (ml_big_parse (&a, ml_str_view (digits)).status == ML_ERR_LIMIT);
		ml_str_free (&digits);
	}

	ml_arena_free (&a);
}

static void test_format_long (void)
{
	MlArena a;
	MlStr   s;
	MlBig   x;

	ml_arena_init (&a, 0);
	ml_str_init (&s);

	/* 2^100000 has 30103 digits: format and parse it back */
	x = ml_big_pow (&a, num (&a, "2"), 100000);
	CHECK (ml_big_ok     (x));
	CHECK (ml_big_format (&s, x) == ML_OK);
	CHECK (s.len == 30103);
	CHECK (strncmp (ml_str_cstr (s), "9990020930143845079440327643", 28) == 0);
	CHECK (ml_big_cmp (ml_big_parse (&a, ml_str_view (s)), x) == 0);

	ml_str_free (&s);
	ml_arena_free (&a);
}

void run_bigint_tests (void)
{
	test_construct_and_format ();
	test_compare ();
	test_add_sub ();
	test_mul ();
	test_divmod ();
	test_gcd ();
	test_pow ();
	test_shift_and_bits ();
	test_conversions ();
	test_isqrt ();
	test_errors ();
	test_format_long ();
}
