#include <matlog/core/bigint.h>

#include <matlog/core/vec.h>

#include <math.h>

#define LIMB_BASE     ((uint64_t)1 << 32)
#define CHUNK_DIGITS  9
#define CHUNK_BASE    1000000000u
#define MAX_DIGITS    31000

ML_VEC_DEFINE (MlU32Vec, uint32_t)

MlBig ml_big_error (MlResult status)
{
	MlBig x =
	{
		.limbs    = NULL,
		.len      = 0,
		.negative = 0,
		.status   = status
	};

	return x;
}

MlBig ml_big_zero (void)
{
	return ml_big_error (ML_OK);
}

/* A value from limbs: leading zeros are dropped, too large is an error. */
static MlBig make (const uint32_t *limbs, uint64_t len, int32_t negative)
{
	uint64_t n = len;
	MlBig    x = ml_big_zero ();

	while (n > 0 && limbs [n - 1] == 0)
	{
		n--;
	}

	if (n > ML_BIG_MAX_LIMBS)
	{
		x = ml_big_error (ML_ERR_LIMIT);
	}

	else if (n > 0)
	{
		x.limbs    = limbs;
		x.len      = n;
		x.negative = negative != 0;
	}

	return x;
}

/* n zeroed limbs from the arena, NULL for n == 0 or when memory is short. */
static uint32_t *limbs_new (MlArena *arena, uint64_t n)
{
	return ml_choose (
		n > 0 && n <= 2 * ML_BIG_MAX_LIMBS + 2,
		ml_arena_alloc_zero (arena, n * sizeof (uint32_t), sizeof (uint32_t)),
		NULL
	);
}

static MlResult status_of (MlBig x, MlBig y)
{
	return ml_choose (x.status != ML_OK, x.status, y.status);
}

int32_t ml_big_ok (MlBig x)
{
	return x.status == ML_OK;
}

int32_t ml_big_is_zero (MlBig x)
{
	return x.len == 0;
}

int32_t ml_big_is_odd (MlBig x)
{
	return x.len > 0 && (x.limbs [0] & 1) != 0;
}

MlBig ml_big_from_u64 (MlArena *arena, uint64_t value)
{
	MlBig    x    = ml_big_zero ();
	uint32_t *buf = limbs_new (arena, 2);

	if (value != 0 && buf == NULL)
	{
		x = ml_big_error (ML_ERR_NOMEM);
	}

	else if (value != 0)
	{
		buf [0] = (uint32_t)value;
		buf [1] = (uint32_t)(value >> 32);
		x       = make (buf, 2, 0);
	}

	return x;
}

MlBig ml_big_from_i64 (MlArena *arena, int64_t value)
{
	MlBig x = ml_big_from_u64 (
		arena,
		ml_choose (
			value < 0,
			(uint64_t)(-(value + 1)) + 1,
			(uint64_t)value
		)
	);

	x.negative = x.len > 0 && value < 0;
	return x;
}

/* Magnitude comparison. */
static int32_t cmp_mag (MlBig x, MlBig y)
{
	int32_t order = ml_choose (
		x.len == y.len,
		0,
		ml_choose (x.len < y.len, -1, 1)
	);

	for (uint64_t i = x.len; i > 0 && order == 0 && x.len == y.len; i--)
	{
		order = ml_choose (
			x.limbs [i - 1] == y.limbs [i - 1],
			0,
			ml_choose (x.limbs [i - 1] < y.limbs [i - 1], -1, 1)
		);
	}

	return order;
}

int32_t ml_big_cmp (MlBig x, MlBig y)
{
	int32_t order = 0;

	if (x.negative != y.negative)
	{
		order = ml_choose (x.negative, -1, 1);
	}

	else
	{
		order = ml_choose (x.negative, -cmp_mag (x, y), cmp_mag (x, y));
	}

	return order;
}

uint64_t ml_big_bit_length (MlBig x)
{
	uint64_t bits = 0;
	uint32_t top;

	if (x.len > 0)
	{
		top  = x.limbs [x.len - 1];
		bits = 32 * (x.len - 1);

		while (top != 0)
		{
			bits++;
			top >>= 1;
		}
	}

	return bits;
}

MlU64Result ml_big_to_u64 (MlBig x)
{
	MlU64Result result =
	{
		.ok    = x.status == ML_OK && !x.negative && x.len <= 2,
		.value = 0
	};

	if (result.ok && x.len > 0)
	{
		result.value = x.limbs [0];
	}

	if (result.ok && x.len > 1)
	{
		result.value |= (uint64_t)x.limbs [1] << 32;
	}

	return result;
}

double ml_big_to_double (MlBig x)
{
	double   value = 0.0;
	uint64_t bits  = ml_big_bit_length (x);

	if (bits > 0 && bits <= 64)
	{
		uint64_t all = x.limbs [0];

		all   |= ml_choose (x.len > 1, (uint64_t)x.limbs [1] << 32, 0);
		value = (double)all;
	}

	else if (bits > 64)
	{
		/* the leading 64 bits, then scaled back up */
		uint64_t pos = bits - 64;
		uint64_t k   = pos / 32;
		uint64_t off = pos % 32;
		uint64_t l0  = x.limbs [k];
		uint64_t l1  = ml_choose (k + 1 < x.len, x.limbs [k + 1], 0);
		uint64_t l2  = ml_choose (k + 2 < x.len, x.limbs [k + 2], 0);
		uint64_t top = ((l1 << 32) | l0) >> off;

		top |= ml_choose (off > 0, l2 << (64 - off), 0);
		value = ldexp ((double)top, (int32_t)pos);
	}

	return ml_choose (x.negative, -value, value);
}

MlBig ml_big_neg (MlArena *arena, MlBig x)
{
	MlBig result = x;

	(void)arena;
	result.negative = x.len > 0 && !x.negative;
	return result;
}

MlBig ml_big_abs (MlArena *arena, MlBig x)
{
	MlBig result = x;

	(void)arena;
	result.negative = 0;
	return result;
}

/* |a| + |b| with the given sign. */
static MlBig add_mag (MlArena *arena, MlBig a, MlBig b, int32_t negative)
{
	uint64_t n      = ml_choose (a.len > b.len, a.len, b.len) + 1;
	uint32_t *buf   = limbs_new (arena, n);
	uint64_t carry  = 0;
	MlBig    result = ml_big_error (ML_ERR_NOMEM);

	if (buf != NULL)
	{
		for (uint64_t i = 0; i < n; i++)
		{
			uint64_t sum = carry;

			sum  += ml_choose (i < a.len, a.limbs [i], 0);
			sum  += ml_choose (i < b.len, b.limbs [i], 0);
			buf [i] = (uint32_t)sum;
			carry   = sum >> 32;
		}

		result = make (buf, n, negative);
	}

	return result;
}

/* |a| - |b| with |a| >= |b|, with the given sign. */
static MlBig sub_mag (MlArena *arena, MlBig a, MlBig b, int32_t negative)
{
	uint32_t *buf   = limbs_new (arena, a.len);
	int64_t  borrow = 0;
	MlBig    result = ml_big_error (ML_ERR_NOMEM);

	if (buf != NULL)
	{
		for (uint64_t i = 0; i < a.len; i++)
		{
			int64_t diff = (int64_t)a.limbs [i] - borrow;

			diff   -= ml_choose (i < b.len, (int64_t)b.limbs [i], 0);
			borrow  = ml_choose (diff < 0, 1, 0);
			buf [i] = (uint32_t)diff;
		}

		result = make (buf, a.len, negative);
	}

	return result;
}

MlBig ml_big_add (MlArena *arena, MlBig x, MlBig y)
{
	MlBig   result = ml_big_error (status_of (x, y));
	int32_t order  = cmp_mag (x, y);

	if (result.status != ML_OK)
	{
		result = ml_big_error (result.status);
	}

	else if (x.negative == y.negative)
	{
		result = add_mag (arena, x, y, x.negative);
	}

	else if (order == 0)
	{
		result = ml_big_zero ();
	}

	else if (order > 0)
	{
		result = sub_mag (arena, x, y, x.negative);
	}

	else
	{
		result = sub_mag (arena, y, x, y.negative);
	}

	return result;
}

MlBig ml_big_sub (MlArena *arena, MlBig x, MlBig y)
{
	return ml_big_add (arena, x, ml_big_neg (arena, y));
}

MlBig ml_big_mul (MlArena *arena, MlBig x, MlBig y)
{
	MlBig    result = ml_big_error (status_of (x, y));
	uint32_t *buf   = NULL;
	uint64_t n      = x.len + y.len;
	int32_t  alloc  = 0;

	if (result.status != ML_OK)
	{
		result = ml_big_error (result.status);
	}

	else if (x.len == 0 || y.len == 0)
	{
		result = ml_big_zero ();
	}

	else if (n - 1 > ML_BIG_MAX_LIMBS)
	{
		result = ml_big_error (ML_ERR_LIMIT);
	}

	else
	{
		buf   = limbs_new (arena, n);
		alloc = 1;
	}

	if (buf != NULL)
	{
		for (uint64_t i = 0; i < x.len; i++)
		{
			uint64_t carry = 0;

			for (uint64_t j = 0; j < y.len; j++)
			{
				uint64_t t = (uint64_t)x.limbs [i] * y.limbs [j]
					+ buf [i + j]
					+ carry;

				buf [i + j] = (uint32_t)t;
				carry       = t >> 32;
			}

			buf [i + y.len] = (uint32_t)carry;
		}

		result = make (buf, n, x.negative != y.negative);
	}

	else if (alloc)
	{
		result = ml_big_error (ML_ERR_NOMEM);
	}

	return result;
}

/* Leading zero bits of a non zero limb. */
static uint32_t clz32 (uint32_t v)
{
	uint32_t count = 0;

	while (count < 32 && (v & 0x80000000u) == 0)
	{
		v <<= 1;
		count++;
	}

	return count;
}

/* The 32 bits of (hi:lo << s) that line up with hi. */
static uint32_t shift_left_pair (uint32_t hi, uint32_t lo, uint32_t s)
{
	uint64_t both = ((uint64_t)hi << 32) | lo;

	return (uint32_t)((both << s) >> 32);
}

/* The low 32 bits of (hi:lo >> s). */
static uint32_t shift_right_pair (uint32_t hi, uint32_t lo, uint32_t s)
{
	uint64_t both = ((uint64_t)hi << 32) | lo;

	return (uint32_t)(both >> s);
}

/* Magnitudes: x / d for a single limb divisor d != 0. */
static MlBigDiv divmod_limb (MlArena *arena, MlBig x, uint32_t d)
{
	MlBigDiv result =
	{
		.quot = ml_big_error (ML_ERR_NOMEM),
		.rem  = ml_big_error (ML_ERR_NOMEM)
	};
	uint32_t *q  = limbs_new (arena, x.len);
	uint64_t rem = 0;

	if (q != NULL)
	{
		for (uint64_t i = x.len; i > 0; i--)
		{
			uint64_t cur = (rem << 32) | x.limbs [i - 1];

			q [i - 1] = (uint32_t)(cur / d);
			rem       = cur % d;
		}

		result.quot = make (q, x.len, 0);
		result.rem  = ml_big_from_u64 (arena, rem);
	}

	return result;
}

/* Magnitudes: Knuth's algorithm D for a divisor of two or more limbs. */
static MlBigDiv divmod_knuth (MlArena *arena, MlBig x, MlBig y)
{
	MlBigDiv result =
	{
		.quot = ml_big_error (ML_ERR_NOMEM),
		.rem  = ml_big_error (ML_ERR_NOMEM)
	};
	uint64_t m  = x.len - y.len;
	uint32_t *u = limbs_new (arena, x.len + 1);
	uint32_t *v = limbs_new (arena, y.len);
	uint32_t *q = limbs_new (arena, m + 1);
	uint32_t *r = limbs_new (arena, y.len);
	uint32_t s  = clz32 (y.limbs [y.len - 1]);

	if (u != NULL && v != NULL && q != NULL && r != NULL)
	{
		/*
		 * normalize: shift both so the top limb of the divisor has its top bit
		 * set
		 */
		for (uint64_t i = y.len; i > 0; i--)
		{
			uint32_t lower = ml_choose (i > 1, y.limbs [i - 2], 0);

			v [i - 1] = shift_left_pair (y.limbs [i - 1], lower, s);
		}

		u [x.len] = shift_left_pair (0, x.limbs [x.len - 1], s);

		for (uint64_t i = x.len; i > 0; i--)
		{
			uint32_t lower = ml_choose (i > 1, x.limbs [i - 2], 0);

			u [i - 1] = shift_left_pair (x.limbs [i - 1], lower, s);
		}

		for (uint64_t k = 0; k <= m; k++)
		{
			uint64_t j      = m - k;
			uint64_t top    = j + y.len;
			uint64_t num    = ((uint64_t)u [top] << 32) | u [top - 1];
			uint64_t qhat   = num / v [y.len - 1];
			uint64_t rhat   = num % v [y.len - 1];
			int32_t  fix    = 1;
			int64_t  borrow = 0;
			uint64_t carry  = 0;

			/*
			 * the estimate is at most two too large; trim it with the next limb
			 */
			while (fix)
			{
				fix = (
					   qhat >= LIMB_BASE
					|| qhat * v [y.len - 2]
						> ((rhat << 32) | u [j + y.len - 2])
				);

				if (fix)
				{
					qhat--;
					rhat += v [y.len - 1];
					fix = rhat < LIMB_BASE;
				}
			}

			/* u[j .. j + len] -= qhat * v */
			for (uint64_t i = 0; i < y.len; i++)
			{
				uint64_t p   = qhat * v [i] + carry;
				int64_t  low = (int64_t)(p & 0xFFFFFFFFu);
				int64_t  t   = (int64_t)u [i + j] - borrow - low;

				carry     = p >> 32;
				u [i + j] = (uint32_t)t;
				borrow    = ml_choose (t < 0, 1, 0);
			}

			{
				int64_t t = (int64_t)u [j + y.len] - borrow - (int64_t)carry;

				u [j + y.len] = (uint32_t)t;

				/* the estimate was one too large: add the divisor back */
				if (t < 0)
				{
					uint64_t add_carry = 0;

					qhat--;

					for (uint64_t i = 0; i < y.len; i++)
					{
						uint64_t sum = (uint64_t)u [i + j] + v [i] + add_carry;

						u [i + j] = (uint32_t)sum;
						add_carry = sum >> 32;
					}

					u [j + y.len] += (uint32_t)add_carry;
				}
			}

			q [j] = (uint32_t)qhat;
		}

		/* the remainder is the low part of u, shifted back */
		for (uint64_t i = 0; i < y.len; i++)
		{
			r [i] = shift_right_pair (u [i + 1], u [i], s);
		}

		result.quot = make (q, m + 1, 0);
		result.rem  = make (r, y.len, 0);
	}

	return result;
}

/* Magnitudes: x / y for y != 0. */
static MlBigDiv divmod_mag (MlArena *arena, MlBig x, MlBig y)
{
	MlBigDiv result;

	if (cmp_mag (x, y) < 0)
	{
		result.quot = ml_big_zero ();
		result.rem  = make (x.limbs, x.len, 0);
	}

	else if (y.len == 1)
	{
		result = divmod_limb (arena, x, y.limbs [0]);
	}

	else
	{
		result = divmod_knuth (arena, x, y);
	}

	return result;
}

MlBigDiv ml_big_divmod (MlArena *arena, MlBig x, MlBig y)
{
	MlBigDiv result =
	{
		.quot = ml_big_error (status_of (x, y)),
		.rem  = ml_big_error (status_of (x, y))
	};

	if (result.quot.status == ML_OK && y.len == 0)
	{
		result.quot = ml_big_error (ML_ERR_DIVZERO);
		result.rem  = ml_big_error (ML_ERR_DIVZERO);
	}

	else if (result.quot.status == ML_OK)
	{
		result = divmod_mag (arena, x, y);

		if (result.quot.status == ML_OK && result.rem.status == ML_OK)
		{
			result.quot.negative = (
				   result.quot.len > 0
				&& x.negative != y.negative
			);
			result.rem.negative = result.rem.len > 0 && x.negative;
		}
	}

	return result;
}

MlBig ml_big_gcd (MlArena *arena, MlBig x, MlBig y)
{
	MlBig a = ml_big_abs (arena, x);
	MlBig b = ml_big_abs (arena, y);

	while (a.status == ML_OK && b.status == ML_OK && b.len > 0)
	{
		MlBig r = ml_big_divmod (arena, a, b).rem;

		a = b;
		b = r;
	}

	return ml_choose (
		b.status == ML_OK,
		a,
		b
	);
}

MlBig ml_big_pow (MlArena *arena, MlBig x, uint64_t exponent)
{
	MlBig    result = ml_big_from_u64 (arena, 1);
	MlBig    base   = x;
	uint64_t e      = exponent;

	if (x.status != ML_OK)
	{
		result = ml_big_error (x.status);
	}

	while (result.status == ML_OK && base.status == ML_OK && e > 0)
	{
		if (e & 1)
		{
			result = ml_big_mul (arena, result, base);
		}

		e >>= 1;

		if (e > 0)
		{
			base = ml_big_mul (arena, base, base);
		}
	}

	return ml_choose (
		base.status == ML_OK,
		result,
		base
	);
}

MlBig ml_big_shl (MlArena *arena, MlBig x, uint64_t bits)
{
	MlBig    result = x;
	uint64_t words  = bits / 32;
	uint64_t shift  = bits % 32;
	uint32_t *buf   = NULL;

	if (x.status != ML_OK || x.len == 0)
	{
		result = ml_choose (
			x.status != ML_OK,
			ml_big_error (x.status),
			ml_big_zero ()
		);
	}

	else if (bits > (uint64_t)ML_BIG_MAX_LIMBS * 32)
	{
		result = ml_big_error (ML_ERR_LIMIT);
	}

	else
	{
		buf = limbs_new (arena, x.len + words + 1);
	}

	if (buf != NULL)
	{
		for (uint64_t i = 0; i < x.len; i++)
		{
			uint64_t v = (uint64_t)x.limbs [i] << shift;

			buf [i + words]     |= (uint32_t)v;
			buf [i + words + 1] |= (uint32_t)(v >> 32);
		}

		result = make (buf, x.len + words + 1, x.negative);
	}

	else if (result.status == ML_OK && x.len > 0)
	{
		result = ml_big_error (ML_ERR_NOMEM);
	}

	return result;
}

MlBig ml_big_shr (MlArena *arena, MlBig x, uint64_t bits)
{
	MlBig    result = x;
	uint64_t words  = bits / 32;
	uint64_t shift  = bits % 32;
	uint32_t *buf   = NULL;

	if (x.status != ML_OK || x.len == 0)
	{
		result = ml_choose (
			x.status != ML_OK,
			ml_big_error (x.status),
			ml_big_zero ()
		);
	}

	else if (words >= x.len)
	{
		result = ml_big_zero ();
	}

	else
	{
		buf = limbs_new (arena, x.len - words);
	}

	if (buf != NULL)
	{
		for (uint64_t i = words; i < x.len; i++)
		{
			uint64_t hi = ml_choose (i + 1 < x.len, x.limbs [i + 1], 0);
			uint64_t v  = ((hi << 32) | x.limbs [i]) >> shift;

			buf [i - words] = (uint32_t)v;
		}

		result = make (buf, x.len - words, x.negative);
	}

	else if (result.status == ML_OK && x.len > 0 && words < x.len)
	{
		result = ml_big_error (ML_ERR_NOMEM);
	}

	return result;
}

MlBig ml_big_isqrt (MlArena *arena, MlBig x)
{
	MlBig   result = ml_big_error (x.status);
	MlBig   one    = ml_big_from_u64 (arena, 1);
	MlBig   guess  = ml_big_zero ();
	int32_t more   = 1;

	if (x.status != ML_OK)
	{
		result = ml_big_error (x.status);
	}

	else if (x.negative)
	{
		result = ml_big_error (ML_ERR_RANGE);
	}

	else if (x.len == 0)
	{
		result = ml_big_zero ();
	}

	else
	{
		/*
		 * Newton's method, starting above the root: it decreases to the floor
		 */
		guess = ml_big_shl (arena, one, (ml_big_bit_length (x) + 1) / 2);

		while (more && guess.status == ML_OK)
		{
			MlBig next = ml_big_shr (
				arena,
				ml_big_add (arena, guess, ml_big_divmod (arena, x, guess).quot),
				1
			);

			more  = next.status == ML_OK && cmp_mag (next, guess) < 0;
			guess = ml_choose (more, next, guess);
		}

		result = guess;
	}

	return result;
}

MlBig ml_big_parse (MlArena *arena, MlSlice text)
{
	MlBig    result   = ml_big_error (ML_ERR_PARSE);
	int32_t  negative = text.len > 0 && text.data [0] == '-';
	uint64_t start    = ml_choose (
		text.len > 0 && (text.data [0] == '-' || text.data [0] == '+'),
		1,
		0
	);
	uint64_t skip  = start;
	uint64_t digits;
	int32_t  valid = text.len > start;
	uint32_t *buf  = NULL;

	for (uint64_t i = start; i < text.len && valid; i++)
	{
		valid = text.data [i] >= '0' && text.data [i] <= '9';
	}

	while (valid && skip < text.len - 1 && text.data [skip] == '0')
	{
		skip++;
	}

	digits = ml_choose (valid, text.len - skip, 0);

	if (!valid)
	{
		result = ml_big_error (ML_ERR_PARSE);
	}

	else if (digits > MAX_DIGITS)
	{
		result = ml_big_error (ML_ERR_LIMIT);
	}

	else
	{
		buf = limbs_new (arena, digits / CHUNK_DIGITS + 2);
	}

	if (buf != NULL)
	{
		uint64_t used = 0;
		uint64_t pos  = 0;

		while (pos < digits)
		{
			uint64_t len = ml_choose (
				pos == 0 && digits % CHUNK_DIGITS != 0,
				digits % CHUNK_DIGITS,
				CHUNK_DIGITS
			);
			uint64_t chunk = 0;
			uint64_t scale = 1;
			uint64_t carry;

			for (uint64_t k = 0; k < len; k++)
			{
				char c = text.data [skip + pos + k];

				chunk = chunk * 10 + (uint64_t)(c - '0');
				scale *= 10;
			}

			carry = chunk;

			for (uint64_t i = 0; i < used; i++)
			{
				uint64_t t = (uint64_t)buf [i] * scale + carry;

				buf [i] = (uint32_t)t;
				carry   = t >> 32;
			}

			if (carry != 0)
			{
				buf [used] = (uint32_t)carry;
				used++;
			}

			pos += len;
		}

		result = make (buf, used, negative);
	}

	else if (valid && digits <= MAX_DIGITS)
	{
		result = ml_big_error (ML_ERR_NOMEM);
	}

	return result;
}

MlResult ml_big_format (MlStr *out, MlBig x)
{
	MlResult r = x.status;
	MlU32Vec work;
	MlU32Vec chunks;

	MlU32Vec_init (&work);
	MlU32Vec_init (&chunks);

	if (r == ML_OK && x.len == 0)
	{
		r = ml_str_append (out, ml_slice_cstr ("0"));
	}

	else if (r == ML_OK)
	{
		uint64_t len = x.len;

		for (uint64_t i = 0; i < x.len && r == ML_OK; i++)
		{
			r = MlU32Vec_push (&work, x.limbs [i]);
		}

		/* peel off nine decimal digits at a time */
		while (r == ML_OK && len > 0)
		{
			uint64_t rem = 0;

			for (uint64_t i = len; i > 0; i--)
			{
				uint64_t cur = (rem << 32) | work.data [i - 1];

				work.data [i - 1] = (uint32_t)(cur / CHUNK_BASE);
				rem               = cur % CHUNK_BASE;
			}

			while (len > 0 && work.data [len - 1] == 0)
			{
				len--;
			}

			r = MlU32Vec_push (&chunks, (uint32_t)rem);
		}

		if (r == ML_OK && x.negative)
		{
			r = ml_str_append (out, ml_slice_cstr ("-"));
		}

		for (uint64_t i = chunks.len; i > 0 && r == ML_OK; i--)
		{
			r = ml_str_appendf (
				out,
				ml_choose (i == chunks.len, "%u", "%09u"),
				chunks.data [i - 1]
			);
		}
	}

	MlU32Vec_free (&chunks);
	MlU32Vec_free (&work);
	return r;
}
