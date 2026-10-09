#include "internal.h"

/*
 * Polynomials in one symbol with rational coefficients, for the integration
 * of rational functions: arithmetic, division with remainder, reading one off
 * an expanded expression, and the search for rational roots. Coefficients
 * live in the arena of the context.
 */

#define MAX_DEGREE   64
#define MAX_DIVISORS 4096

MlPoly ml_poly_zero (void)
{
	MlPoly p =
	{
		.c   = NULL,
		.len = 0
	};

	return p;
}

MlPoly ml_poly_make (MlSymCtx *ctx, uint64_t len)
{
	MlPoly p = ml_poly_zero ();

	if (len > 0 && ctx->status == ML_OK)
	{
		p.c = ML_ARENA_NEW_ARRAY (ctx->arena, MlRat, len);

		if (p.c == NULL)
		{
			ctx->status = ML_ERR_NOMEM;
		}

		else
		{
			p.len = len;

			for (uint64_t i = 0; i < len; i++)
			{
				p.c [i] = ml_rat_zero (ctx->arena);
			}
		}
	}

	return p;
}

MlPoly ml_poly_trim (MlPoly p)
{
	while (p.len > 0 && ml_rat_is_zero (p.c [p.len - 1]))
	{
		p.len--;
	}

	return p;
}

int32_t ml_poly_is_zero (MlPoly p)
{
	return ml_poly_trim (p).len == 0;
}

MlRat ml_poly_lead (MlPoly p)
{
	MlPoly t = ml_poly_trim (p);

	return ml_choose (
		t.len > 0,
		t.c [ml_choose (t.len > 0, t.len - 1, 0)],
		ml_rat_error (ML_ERR_INVALID)
	);
}

static MlRat coeff (MlPoly p, uint64_t i)
{
	return ml_choose (
		i < p.len,
		p.c [ml_choose (i < p.len, i, 0)],
		ml_rat_error (ML_OK)
	);
}

MlPoly ml_poly_add (MlSymCtx *ctx, MlPoly a, MlPoly b)
{
	uint64_t n = ml_choose (a.len > b.len, a.len, b.len);
	MlPoly   r = ml_poly_make (ctx, n);

	for (uint64_t i = 0; i < n && ctx->status == ML_OK; i++)
	{
		MlRat x = ml_choose (i < a.len, coeff (a, i), ml_rat_zero (ctx->arena));
		MlRat y = ml_choose (i < b.len, coeff (b, i), ml_rat_zero (ctx->arena));

		r.c [i] = ml_rat_add (ctx->arena, x, y);
	}

	return ml_poly_trim (r);
}

MlPoly ml_poly_scale (MlSymCtx *ctx, MlPoly a, MlRat k)
{
	MlPoly r = ml_poly_make (ctx, a.len);

	for (uint64_t i = 0; i < a.len && ctx->status == ML_OK; i++)
	{
		r.c [i] = ml_rat_mul (ctx->arena, a.c [i], k);
	}

	return ml_poly_trim (r);
}

MlPoly ml_poly_sub (MlSymCtx *ctx, MlPoly a, MlPoly b)
{
	return ml_poly_add (
		ctx,
		a,
		ml_poly_scale (ctx, b, ml_rat_from_i64 (ctx->arena, -1))
	);
}

MlPoly ml_poly_mul (MlSymCtx *ctx, MlPoly a, MlPoly b)
{
	MlPoly r = ml_poly_zero ();

	if (a.len > 0 && b.len > 0)
	{
		r = ml_poly_make (ctx, a.len + b.len - 1);
	}

	for (uint64_t i = 0; i < a.len && ctx->status == ML_OK; i++)
	{
		for (uint64_t j = 0; j < b.len && ctx->status == ML_OK; j++)
		{
			r.c [i + j] = ml_rat_add (
				ctx->arena,
				r.c [i + j],
				ml_rat_mul (ctx->arena, a.c [i], b.c [j])
			);
		}
	}

	return ml_poly_trim (r);
}

void ml_poly_divmod (
	MlSymCtx *ctx,
	MlPoly   a,
	MlPoly   b,
	MlPoly   *q,
	MlPoly   *r
)
{
	MlPoly rem  = ml_poly_make (ctx, a.len);
	MlPoly quot = ml_poly_zero ();
	MlRat  lead = ml_poly_lead (b);

	b = ml_poly_trim (b);

	for (uint64_t i = 0; i < a.len && ctx->status == ML_OK; i++)
	{
		rem.c [i] = a.c [i];
	}

	if (b.len > 0 && a.len >= b.len)
	{
		quot = ml_poly_make (ctx, a.len - b.len + 1);

		for (uint64_t k = a.len - b.len + 1; k > 0 && ctx->status == ML_OK; k--)
		{
			uint64_t at   = k - 1;
			MlRat    coef = ml_rat_div (
				ctx->arena,
				rem.c [at + b.len - 1],
				lead
			);

			quot.c [at] = coef;

			for (uint64_t j = 0; j < b.len; j++)
			{
				rem.c [at + j] = ml_rat_sub (
					ctx->arena,
					rem.c [at + j],
					ml_rat_mul (ctx->arena, coef, b.c [j])
				);
			}
		}

		rem.len = b.len - 1;
	}

	*q = ml_poly_trim (quot);
	*r = ml_poly_trim (rem);
}

/* ---- from an expression ---- */

/* The coefficient and degree of one term; 0 if it is not a monomial of x. */
static int32_t term_parts (
	const MlSym *t,
	const MlSym *x,
	MlRat       *coef,
	uint64_t    *degree,
	MlSymCtx    *ctx
)
{
	int32_t  ok    = 1;
	uint64_t count = ml_choose (t->kind == ML_SYM_MUL, t->count, 1);
	int32_t  seen  = 0;

	*coef   = ml_rat_from_i64 (ctx->arena, 1);
	*degree = 0;

	for (uint64_t i = 0; i < count && ok; i++)
	{
		const MlSym *f = ml_choose (t->kind == ML_SYM_MUL, t->kids [i], t);
		int64_t     k  = 0;

		if (f->kind == ML_SYM_NUM)
		{
			*coef = ml_rat_mul (ctx->arena, *coef, f->num);
		}

		else if (!seen && ml_sym_compare (ctx, f, x) == 0)
		{
			*degree = 1;
			seen    = 1;
		}

		else if (
			   !seen
			&& f->kind == ML_SYM_POW
			&& ml_sym_compare (ctx, f->kids [0], x) == 0
			&& ml_sym_to_i64 (f->kids [1], &k)
			&& k >= 0
			&& k <= MAX_DEGREE
		)
		{
			*degree = (uint64_t)k;
			seen    = 1;
		}

		else
		{
			ok = 0;
		}
	}

	return ok;
}

int32_t ml_poly_from_sym (
	MlSymCtx    *ctx,
	const MlSym *s,
	const MlSym *x,
	MlPoly      *out
)
{
	uint64_t count  = ml_choose (s->kind == ML_SYM_ADD, s->count, 1);
	uint64_t top    = 0;
	int32_t  ok     = 1;
	MlPoly   result = ml_poly_zero ();

	for (uint64_t pass = 0; pass < 2 && ok && ctx->status == ML_OK; pass++)
	{
		if (pass == 1)
		{
			result = ml_poly_make (ctx, top + 1);
		}

		for (uint64_t i = 0; i < count && ok && ctx->status == ML_OK; i++)
		{
			const MlSym *t = ml_choose (s->kind == ML_SYM_ADD, s->kids [i], s);
			MlRat       c;
			uint64_t    d = 0;

			ok = term_parts (t, x, &c, &d, ctx);

			if (ok && pass == 0)
			{
				top = ml_choose (d > top, d, top);
			}

			else if (ok)
			{
				result.c [d] = ml_rat_add (ctx->arena, result.c [d], c);
			}
		}
	}

	*out = ml_poly_trim (result);
	return ok && ctx->status == ML_OK;
}

const MlSym *ml_poly_to_sym (MlSymCtx *ctx, MlPoly p, const MlSym *x)
{
	SymPtrVec terms;
	const MlSym *out;

	SymPtrVec_init (&terms);

	for (uint64_t i = 0; i < p.len; i++)
	{
		const MlSym *term = ml_sym_mul (
			ctx,
			ml_sym_num (ctx, p.c [i]),
			ml_sym_pow (ctx, x, ml_sym_int (ctx, (int64_t)i))
		);

		if (SymPtrVec_push (&terms, term) != ML_OK)
		{
			ctx->status = ML_ERR_NOMEM;
		}
	}

	out = ml_sym_sum (ctx, terms.data, terms.len);
	SymPtrVec_free (&terms);
	return out;
}

/* ---- rational roots ---- */

/* n as a signed number if the rational is a whole number that fits. */
static int32_t whole_i64 (MlRat r, int64_t *n)
{
	MlU64Result u = ml_big_to_u64 (r.num);
	MlBig       mag = r.num;
	int32_t     ok;

	mag.negative = 0;
	u            = ml_big_to_u64 (mag);
	ok           = ml_rat_is_integer (r) && u.ok && u.value < (1ull << 62);
	*n           = ml_choose (
		ok,
		ml_choose (r.num.negative, -(int64_t)u.value, (int64_t)u.value),
		0
	);
	return ok;
}

/* The divisors of n > 0 into out; the count (0 if there are too many). */
static uint64_t divisors (uint64_t n, uint64_t *out)
{
	uint64_t count = 0;

	for (uint64_t d = 1; d * d <= n && count + 2 <= MAX_DIVISORS; d++)
	{
		if (n % d == 0)
		{
			out [count++] = d;

			if (d != n / d)
			{
				out [count++] = n / d;
			}
		}
	}

	return count;
}

static MlRat eval_rat (MlSymCtx *ctx, MlPoly p, MlRat at)
{
	MlRat v = ml_rat_zero (ctx->arena);

	for (uint64_t i = p.len; i > 0; i--)
	{
		v = ml_rat_add (
			ctx->arena,
			ml_rat_mul (ctx->arena, v, at),
			p.c [i - 1]
		);
	}

	return v;
}

/* Divides p by (x - root) as often as it goes; returns how often. */
static uint64_t remove_root (MlSymCtx *ctx, MlPoly *p, MlRat root)
{
	uint64_t times = 0;
	int32_t  more  = 1;
	MlPoly   lin   = ml_poly_make (ctx, 2);

	if (lin.len == 2)
	{
		lin.c [0] = ml_rat_neg (ctx->arena, root);
		lin.c [1] = ml_rat_from_i64 (ctx->arena, 1);
	}

	while (more && p->len > 1 && ctx->status == ML_OK)
	{
		MlPoly q;
		MlPoly r;

		ml_poly_divmod (ctx, *p, lin, &q, &r);
		more = r.len == 0;

		if (more)
		{
			*p = q;
			times++;
		}
	}

	return times;
}

static int32_t root_known (MlSymCtx *ctx, const MlPolyRoots *r, MlRat cand)
{
	int32_t known = 0;

	for (uint64_t i = 0; i < r->count && !known; i++)
	{
		known = ml_rat_cmp (ctx->arena, r->root [i], cand) == 0;
	}

	return known;
}

MlPolyRoots ml_poly_roots (MlSymCtx *ctx, MlPoly p)
{
	MlPolyRoots out;
	MlPoly      cur  = ml_poly_trim (p);
	MlBig       den  = ml_big_from_i64 (ctx->arena, 1);
	MlPoly      ints;
	int64_t     a0   = 0;
	int64_t     an   = 0;
	uint64_t    ds0 [MAX_DIVISORS];
	uint64_t    dsn [MAX_DIVISORS];
	uint64_t    n0   = 0;
	uint64_t    nn   = 0;

	out.ok    = 0;
	out.count = 0;
	out.rest  = cur;
	out.lead  = ml_poly_lead (cur);

	if (cur.len > 1)
	{
		/* integer coefficients: multiply by the common denominator */
		for (uint64_t i = 0; i < cur.len; i++)
		{
			MlBig g = ml_big_gcd (ctx->arena, den, cur.c [i].den);

			den = ml_big_mul (
				ctx->arena,
				den,
				ml_big_divmod (ctx->arena, cur.c [i].den, g).quot
			);
		}

		ints = ml_poly_scale (
			ctx,
			cur,
			ml_rat_from_big (ctx->arena, den)
		);
		out.ok = 1;

		/* a zero constant term: x is a root, and the rest has one */
		if (ml_rat_is_zero (ints.c [0]))
		{
			MlRat zero = ml_rat_zero (ctx->arena);

			out.root [0] = zero;
			out.mult [0] = remove_root (ctx, &ints, zero);
			out.count    = 1;
			cur          = ints;
		}

		else
		{
			cur = ints;
		}

		if (cur.len > 1)
		{
			out.ok = (
				   whole_i64 (cur.c [0], &a0)
				&& whole_i64 (cur.c [cur.len - 1], &an)
				&& a0 != 0
				&& an != 0
			);
		}

		if (out.ok && cur.len > 1)
		{
			n0 = divisors ((uint64_t)(ml_choose (a0 < 0, -a0, a0)), ds0);
			nn = divisors ((uint64_t)(ml_choose (an < 0, -an, an)), dsn);
			out.ok = n0 > 0 && nn > 0;
		}

		for (uint64_t i = 0; out.ok && i < n0 && cur.len > 1; i++)
		{
			for (uint64_t j = 0; j < nn && cur.len > 1; j++)
			{
				for (int32_t sg = 1; sg >= -1; sg -= 2)
				{
					MlRat cand = ml_rat_make (
						ctx->arena,
						ml_big_from_i64 (ctx->arena, sg * (int64_t)ds0 [i]),
						ml_big_from_u64 (ctx->arena, dsn [j])
					);

					if (
						   out.count < ML_POLY_MAX_ROOTS
						&& cur.len > 1
						&& !root_known (ctx, &out, cand)
						&& ml_rat_is_zero (eval_rat (ctx, cur, cand))
					)
					{
						out.root [out.count] = cand;
						out.mult [out.count] = remove_root (ctx, &cur, cand);
						out.count++;
					}
				}
			}
		}

		if (out.ok)
		{
			/* what is left, made monic */
			MlRat lead = ml_poly_lead (cur);

			out.rest
				= ml_poly_scale (ctx, cur, ml_rat_recip (ctx->arena, lead));
		}
	}

	return out;
}
