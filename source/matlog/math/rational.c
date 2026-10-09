#include <matlog/math/rational.h>

#include <math.h>

MlRat ml_rat_error (MlResult status)
{
	MlRat x =
	{
		.num = ml_big_error (status),
		.den = ml_big_error (status)
	};

	return x;
}

MlRat ml_rat_zero (MlArena *arena)
{
	MlRat x =
	{
		.num = ml_big_zero (),
		.den = ml_big_from_u64 (arena, 1)
	};

	return x;
}

MlResult ml_rat_status (MlRat x)
{
	return ml_choose (x.num.status != ML_OK, x.num.status, x.den.status);
}

int32_t ml_rat_ok (MlRat x)
{
	return ml_rat_status (x) == ML_OK;
}

static MlBig big_one (MlArena *arena)
{
	return ml_big_from_u64 (arena, 1);
}

/* n / d for d > 0 that is already in lowest terms. */
static MlRat raw (MlBig n, MlBig d)
{
	MlRat x =
	{
		.num = n,
		.den = d
	};

	return x;
}

MlRat ml_rat_from_big (MlArena *arena, MlBig n)
{
	return raw (n, big_one (arena));
}

MlRat ml_rat_from_i64 (MlArena *arena, int64_t n)
{
	return raw (ml_big_from_i64 (arena, n), big_one (arena));
}

MlRat ml_rat_make (MlArena *arena, MlBig num, MlBig den)
{
	MlRat    result = ml_rat_error (ML_OK);
	MlResult status = ml_choose (num.status != ML_OK, num.status, den.status);
	MlBig    g;
	MlBig    n      = num;
	MlBig    d      = den;

	if (status != ML_OK)
	{
		result = ml_rat_error (status);
	}

	else if (den.len == 0)
	{
		result = ml_rat_error (ML_ERR_DIVZERO);
	}

	else if (num.len == 0)
	{
		result = raw (ml_big_zero (), big_one (arena));
	}

	else
	{
		if (d.negative)
		{
			n = ml_big_neg (arena, n);
			d = ml_big_neg (arena, d);
		}

		g = ml_big_gcd (arena, n, d);

		if (g.status == ML_OK && !(g.len == 1 && g.limbs [0] == 1))
		{
			n = ml_big_divmod (arena, n, g).quot;
			d = ml_big_divmod (arena, d, g).quot;
		}

		result = ml_choose (
			g.status == ML_OK,
			raw (n, d),
			ml_rat_error (g.status)
		);
	}

	return result;
}

MlRat ml_rat_parse_decimal (MlArena *arena, MlSlice text)
{
	MlRat    result   = ml_rat_error (ML_ERR_PARSE);
	int32_t  negative = text.len > 0 && text.data [0] == '-';
	uint64_t start    = ml_choose (
		text.len > 0 && (text.data [0] == '-' || text.data [0] == '+'),
		1,
		0
	);
	uint64_t dot   = text.len;
	uint64_t dots  = 0;
	int32_t  valid = text.len > start;
	MlStr    digits;

	ml_str_init (&digits);

	for (uint64_t i = start; i < text.len && valid; i++)
	{
		char c = text.data [i];

		if (c == '.')
		{
			dot = i;
			dots++;
		}

		valid = (c >= '0' && c <= '9') || (c == '.' && dots == 1);
	}

	/* a dot needs at least one digit after it ("5." is not a number) */
	valid = valid && dots <= 1 && (dots == 0 || dot + 1 < text.len);

	if (valid)
	{
		uint64_t frac = ml_choose (dots == 1, text.len - dot - 1, 0);
		MlBig    n;
		MlBig    d;
		MlResult r    = ml_str_append (
			&digits,
			ml_slice_sub (
				text,
				start,
				ml_choose (dots == 1, dot - start, text.len - start)
			)
		);

		if (r == ML_OK && dots == 1)
		{
			r = ml_str_append (&digits, ml_slice_sub (text, dot + 1, frac));
		}

		n = ml_choose (
			r == ML_OK,
			ml_big_parse (arena, ml_str_view (digits)),
			ml_big_error (r)
		);
		n.negative = n.len > 0 && negative;
		d          = ml_big_pow (arena, ml_big_from_u64 (arena, 10), frac);
		result     = ml_rat_make (arena, n, d);
	}

	ml_str_free (&digits);
	return result;
}

int32_t ml_rat_is_zero (MlRat x)
{
	return x.num.len == 0;
}

int32_t ml_rat_is_integer (MlRat x)
{
	return x.den.len == 1 && x.den.limbs [0] == 1;
}

int32_t ml_rat_sign (MlRat x)
{
	return ml_choose (x.num.len == 0, 0, ml_choose (x.num.negative, -1, 1));
}

int32_t ml_rat_cmp (MlArena *arena, MlRat x, MlRat y)
{
	return ml_big_cmp (
		ml_big_mul (arena, x.num, y.den),
		ml_big_mul (arena, y.num, x.den)
	);
}

MlRat ml_rat_neg (MlArena *arena, MlRat x)
{
	return raw (ml_big_neg (arena, x.num), x.den);
}

MlRat ml_rat_abs (MlArena *arena, MlRat x)
{
	return raw (ml_big_abs (arena, x.num), x.den);
}

MlRat ml_rat_add (MlArena *arena, MlRat x, MlRat y)
{
	MlResult status = ml_choose (
		!ml_rat_ok (x),
		ml_rat_status (x),
		ml_rat_status (y)
	);

	return ml_choose (
		status == ML_OK,
		ml_rat_make (
			arena,
			ml_big_add (
				arena,
				ml_big_mul (arena, x.num, y.den),
				ml_big_mul (arena, y.num, x.den)
			),
			ml_big_mul (arena, x.den, y.den)
		),
		ml_rat_error (status)
	);
}

MlRat ml_rat_sub (MlArena *arena, MlRat x, MlRat y)
{
	return ml_rat_add (arena, x, ml_rat_neg (arena, y));
}

MlRat ml_rat_mul (MlArena *arena, MlRat x, MlRat y)
{
	MlResult status = ml_choose (
		!ml_rat_ok (x),
		ml_rat_status (x),
		ml_rat_status (y)
	);

	return ml_choose (
		status == ML_OK,
		ml_rat_make (
			arena,
			ml_big_mul (arena, x.num, y.num),
			ml_big_mul (arena, x.den, y.den)
		),
		ml_rat_error (status)
	);
}

MlRat ml_rat_recip (MlArena *arena, MlRat x)
{
	MlRat result = ml_rat_error (ml_rat_status (x));

	if (result.num.status == ML_OK && x.num.len == 0)
	{
		result = ml_rat_error (ML_ERR_DIVZERO);
	}

	else if (result.num.status == ML_OK)
	{
		/* swapping keeps the fraction reduced; only the sign moves */
		result = raw (
			ml_choose (x.num.negative, ml_big_neg (arena, x.den), x.den),
			ml_big_abs (arena, x.num)
		);
	}

	return result;
}

MlRat ml_rat_div (MlArena *arena, MlRat x, MlRat y)
{
	return ml_rat_mul (arena, x, ml_rat_recip (arena, y));
}

MlRat ml_rat_pow (MlArena *arena, MlRat x, int64_t exponent)
{
	MlRat    result = ml_rat_error (ml_rat_status (x));
	uint64_t e      = ml_choose (
		exponent < 0,
		(uint64_t)(-(exponent + 1)) + 1,
		(uint64_t)exponent
	);
	MlBig n;
	MlBig d;

	if (result.num.status == ML_OK && exponent < 0 && x.num.len == 0)
	{
		result = ml_rat_error (ML_ERR_DIVZERO);
	}

	else if (result.num.status == ML_OK)
	{
		/* powers of coprime numbers stay coprime: no reduction is needed */
		n = ml_big_pow (arena, x.num, e);
		d = ml_big_pow (arena, x.den, e);

		if (exponent < 0)
		{
			MlBig t = n;

			n = ml_choose (t.negative, ml_big_neg (arena, d), d);
			d = ml_big_abs (arena, t);
		}

		result = ml_choose (
			n.status == ML_OK && d.status == ML_OK,
			raw (n, d),
			ml_rat_error (ml_choose (n.status != ML_OK, n.status, d.status))
		);
	}

	return result;
}

MlRatSqrt ml_rat_sqrt_exact (MlArena *arena, MlRat x)
{
	MlRatSqrt result =
	{
		.exact = 0,
		.value = ml_rat_error (ML_ERR_RANGE)
	};

	if (ml_rat_ok (x) && !x.num.negative)
	{
		MlBig sn = ml_big_isqrt (arena, x.num);
		MlBig sd = ml_big_isqrt (arena, x.den);

		result.exact = (
			   sn.status == ML_OK
			&& sd.status == ML_OK
			&& ml_big_cmp (ml_big_mul (arena, sn, sn), x.num) == 0
			&& ml_big_cmp (ml_big_mul (arena, sd, sd), x.den) == 0
		);
		result.value = ml_choose (
			result.exact,
			raw (sn, sd),
			ml_rat_error (ML_ERR_RANGE)
		);
	}

	return result;
}

/* The leading 64 bits of x as a double and the power of two they stand for. */
typedef struct Scaled
{
	double  mantissa;
	int64_t exponent;
} Scaled;

static Scaled scale_down (MlArena *arena, MlBig x)
{
	uint64_t bits  = ml_big_bit_length (x);
	uint64_t shift = ml_choose (bits > 64, bits - 64, 0);
	Scaled   s     =
	{
		.mantissa = ml_big_to_double (ml_big_shr (arena, x, shift)),
		.exponent = (int64_t)shift
	};

	return s;
}

double ml_rat_to_double (MlArena *arena, MlRat x)
{
	Scaled n = scale_down (arena, x.num);
	Scaled d = scale_down (arena, x.den);

	return ml_choose (
		x.num.len == 0 || d.mantissa == 0.0,
		0.0,
		ldexp (n.mantissa / d.mantissa, (int32_t)(n.exponent - d.exponent))
	);
}

MlResult ml_rat_format (MlStr *out, MlRat x)
{
	MlResult r = ml_rat_status (x);

	if (r == ML_OK)
	{
		r = ml_big_format (out, x.num);
	}

	if (r == ML_OK && !ml_rat_is_integer (x))
	{
		r = ml_str_append (out, ml_slice_cstr ("/"));

		if (r == ML_OK)
		{
			r = ml_big_format (out, x.den);
		}
	}

	return r;
}

MlRatDecimal ml_rat_format_decimal (
	MlStr    *out,
	MlArena  *arena,
	MlRat    x,
	uint64_t max_digits
)
{
	MlRatDecimal result =
	{
		.result = ml_rat_status (x),
		.exact  = 0
	};

	MlBigDiv parts;
	MlBig    rem;
	MlBig    ten     = ml_big_from_u64 (arena, 10);
	uint64_t written = 0;

	if (result.result == ML_OK)
	{
		parts = ml_big_divmod (arena, ml_big_abs (arena, x.num), x.den);
		rem   = parts.rem;

		if (x.num.negative)
		{
			result.result = ml_str_append (out, ml_slice_cstr ("-"));
		}

		if (result.result == ML_OK)
		{
			result.result = ml_big_format (out, parts.quot);
		}

		result.exact = rem.status == ML_OK && rem.len == 0;

		if (result.result == ML_OK && rem.len > 0 && max_digits > 0)
		{
			result.result = ml_str_append (out, ml_slice_cstr ("."));
		}

		/* long division, one digit at a time */
		while (
			   result.result == ML_OK
			&& rem.status == ML_OK
			&& rem.len > 0
			&& written < max_digits
		)
		{
			MlBigDiv digit = ml_big_divmod (
				arena,
				ml_big_mul (arena, rem, ten),
				x.den
			);
			MlU64Result value = ml_big_to_u64 (digit.quot);

			result.result = ml_choose (
				value.ok,
				ml_str_appendf (out, "%u", (uint32_t)value.value),
				ML_ERR_LIMIT
			);
			rem = digit.rem;
			written++;
		}

		result.exact = rem.status == ML_OK && rem.len == 0;

		if (result.result == ML_OK && rem.status != ML_OK)
		{
			result.result = rem.status;
		}
	}

	return result;
}

MlRat ml_rat_copy (MlArena *arena, MlRat x)
{
	MlRat out =
	{
		.num = ml_big_copy (arena, x.num),
		.den = ml_big_copy (arena, x.den)
	};

	return out;
}
