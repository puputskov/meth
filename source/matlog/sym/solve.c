#include "internal.h"

/*
 * Solving f = 0 for one symbol, over the real numbers:
 *
 *	a polynomial       the rational roots (rational root theorem), then the
 *	                   quadratic formula for what is left; a polynomial in x^k
 *	                   is solved for x^k and the roots taken
 *	a quotient         the roots of the numerator that are not zeros of the
 *	                   denominator (sums of fractions are put over one line)
 *	other expressions  a polynomial in one inner part u of the expression
 *	                   (exp (x), ln (x), a power or a root of x): solved for
 *	                   u, then u = value is turned into x
 *
 * Every candidate is checked in the equation before it is believed.
 */

#define MAX_SETS 64

static const MlSym *int_node (MlSymCtx *ctx, int64_t n)
{
	return ml_sym_int (ctx, n);
}

static void add (MlSymCtx *ctx, SymPtrVec *out, const MlSym *s)
{
	if (SymPtrVec_push (out, s) != ML_OK)
	{
		ctx->status = ML_ERR_NOMEM;
	}
}

static int32_t sign_of (MlSymCtx *ctx, const MlSym *v)
{
	int32_t sign = 0;
	double  d;

	if (v->kind == ML_SYM_NUM)
	{
		sign = ml_rat_sign (v->num);
	}

	else if (ml_sym_to_double (ctx->arena, v, &d))
	{
		sign = ml_choose (d > 1e-12, 1, ml_choose (d < -1e-12, -1, 0));
	}

	return sign;
}

/* ---- polynomials ---- */

/* The real roots of p into out; 0 when p is not within reach. */
static int32_t poly_roots (MlSymCtx *ctx, MlPoly p, SymPtrVec *out)
{
	MlPoly      t   = ml_poly_trim (p);
	int32_t     ok  = t.len > 0;
	MlPolyRoots r;

	if (ok && t.len > 1)
	{
		r  = ml_poly_roots (ctx, t);
		ok = r.ok && (r.rest.len == 1 || r.rest.len == 3);

		for (uint64_t i = 0; ok && i < r.count; i++)
		{
			add (ctx, out, ml_sym_num (ctx, r.root [i]));
		}

		if (ok && r.rest.len == 3)
		{
			/* x^2 + p x + s: real roots only when p^2 - 4 s is positive */
			MlRat pp   = r.rest.c [1];
			MlRat s    = r.rest.c [0];
			MlRat disc = ml_rat_sub (
				ctx->arena,
				ml_rat_mul (ctx->arena, pp, pp),
				ml_rat_mul (ctx->arena, ml_rat_from_i64 (ctx->arena, 4), s)
			);

			if (ml_rat_sign (disc) > 0)
			{
				const MlSym *root = ml_rat_root_node (ctx, disc);
				const MlSym *mid
					= ml_sym_num (ctx, ml_rat_neg (ctx->arena, pp));

				add (
					ctx,
					out,
					ml_sym_div (
						ctx,
						ml_sym_sub (ctx, mid, root),
						int_node (ctx, 2)
					)
				);
				add (
					ctx,
					out,
					ml_sym_div (
						ctx,
						ml_sym_add (ctx, mid, root),
						int_node (ctx, 2)
					)
				);
			}
		}
	}

	return ok && ctx->status == ML_OK;
}

/* The inverse of u = w0 for the inner parts the engine knows. */
static int32_t invert (
	MlSymCtx    *ctx,
	const MlSym *u,
	const MlSym *w0,
	const MlSym *x,
	SymPtrVec   *out
)
{
	const MlSym *a    = NULL;
	const MlSym *b    = NULL;
	const MlSym *base = NULL;
	int32_t     ok    = 0;
	int32_t     sg    = sign_of (ctx, w0);
	int64_t     n     = 0;

	if (
		   u->kind == ML_SYM_FUNC
		&& u->count == 1
		&& ml_sym_linear_parts (ctx, u->kids [0], x, &a, &b)
	)
	{
		const MlSym *value = NULL;

		if (ml_slice_equal (u->name, ml_slice_cstr ("exp")) && sg > 0)
		{
			value = ml_sym_call (ctx, ml_slice_cstr ("ln"), w0);
		}

		else if (ml_slice_equal (u->name, ml_slice_cstr ("ln")))
		{
			value = ml_sym_call (ctx, ml_slice_cstr ("exp"), w0);
		}

		if (value != NULL)
		{
			add (ctx, out, ml_sym_div (ctx, ml_sym_sub (ctx, value, b), a));
			ok = 1;
		}
	}

	else if (u->kind == ML_SYM_POW
	         && ml_sym_linear_parts (ctx, u->kids [0], x, &a, &b))
	{
		const MlSym *e     = u->kids [1];
		const MlSym *one_n = NULL;

		if (ml_sym_to_i64 (e, &n) && n >= 2)
		{
			one_n = ml_sym_div (ctx, int_node (ctx, 1), int_node (ctx, n));

			if (n % 2 == 0 && sg >= 0)
			{
				const MlSym *r = ml_sym_pow (ctx, w0, one_n);

				add (ctx, out, ml_sym_div (ctx, ml_sym_sub (ctx, r, b), a));

				if (sg > 0)
				{
					add (
						ctx,
						out,
						ml_sym_div (
							ctx,
							ml_sym_sub (ctx, ml_sym_neg (ctx, r), b),
							a
						)
					);
				}

				ok = 1;
			}

			else if (n % 2 != 0)
			{
				const MlSym *r = ml_choose (
					sg >= 0,
					ml_sym_pow (ctx, w0, one_n),
					ml_sym_neg (
						ctx,
						ml_sym_pow (ctx, ml_sym_neg (ctx, w0), one_n)
					)
				);

				add (ctx, out, ml_sym_div (ctx, ml_sym_sub (ctx, r, b), a));
				ok = 1;
			}
		}

		else if (
			   e->kind == ML_SYM_NUM
			&& ml_rat_cmp (
				   ctx->arena,
				   e->num,
				   ml_rat_make (
					   ctx->arena,
					   ml_big_from_i64 (ctx->arena, 1),
					   ml_big_from_i64 (ctx->arena, 2)
				   )
			   ) == 0
			&& sg >= 0
		)
		{
			base = ml_sym_pow (ctx, w0, int_node (ctx, 2));
			add (ctx, out, ml_sym_div (ctx, ml_sym_sub (ctx, base, b), a));
			ok = 1;
		}

		else if (ml_sym_to_i64 (e, &n) && n == -1 && sg != 0)
		{
			base = ml_sym_div (ctx, int_node (ctx, 1), w0);
			add (ctx, out, ml_sym_div (ctx, ml_sym_sub (ctx, base, b), a));
			ok = 1;
		}
	}

	return ok;
}

/* The greatest common divisor of the exponents that carry a coefficient. */
static uint64_t exponent_gcd (MlPoly p)
{
	uint64_t g = 0;

	for (uint64_t i = 1; i < p.len; i++)
	{
		if (!ml_rat_is_zero (p.c [i]))
		{
			uint64_t a = g;
			uint64_t b = i;

			while (b != 0)
			{
				uint64_t t = a % b;

				a = b;
				b = t;
			}

			g = a;
		}
	}

	return g;
}

/* A polynomial in x^k: solve for x^k, then take the k-th roots. */
static int32_t composite_roots (
	MlSymCtx    *ctx,
	MlPoly      p,
	const MlSym *x,
	SymPtrVec   *out
)
{
	uint64_t k  = exponent_gcd (p);
	int32_t  ok = k >= 2 && p.len > 1;

	if (ok)
	{
		MlPoly    q       = ml_poly_make (ctx, (p.len - 1) / k + 1);
		SymPtrVec ws;
		const MlSym *u    = ml_sym_pow (ctx, x, int_node (ctx, (int64_t)k));

		SymPtrVec_init (&ws);

		for (uint64_t i = 0; i < p.len; i += k)
		{
			q.c [i / k] = p.c [i];
		}

		ok = poly_roots (ctx, q, &ws);

		for (uint64_t i = 0; ok && i < ws.len; i++)
		{
			/* a root w that has no real k-th root just adds nothing */
			invert (ctx, u, ws.data [i], x, out);
		}

		SymPtrVec_free (&ws);
	}

	return ok && ctx->status == ML_OK;
}

/* ---- sums of fractions ---- */

/* e = n / d with polynomials, putting a sum of fractions over one line. */
static int32_t together (
	MlSymCtx    *ctx,
	const MlSym *e,
	const MlSym *x,
	MlPoly      *num,
	MlPoly      *den
)
{
	uint64_t count = ml_choose (e->kind == ML_SYM_ADD, e->count, 1);
	MlPoly   *ns   = ML_ARENA_NEW_ARRAY (ctx->arena, MlPoly, count);
	MlPoly   *ds   = ML_ARENA_NEW_ARRAY (ctx->arena, MlPoly, count);
	int32_t  ok    = ns != NULL && ds != NULL && count <= 8;
	MlPoly   total = ml_poly_zero ();
	MlPoly   all   = ml_poly_make (ctx, 1);

	if (all.len == 1)
	{
		all.c [0] = ml_rat_from_i64 (ctx->arena, 1);
	}

	for (uint64_t i = 0; ok && i < count; i++)
	{
		const MlSym *t = ml_choose (e->kind == ML_SYM_ADD, e->kids [i], e);

		ds [i] = ml_poly_make (ctx, 1);

		if (ds [i].len == 1)
		{
			ds [i].c [0] = ml_rat_from_i64 (ctx->arena, 1);
		}

		ok = ml_poly_from_sym (ctx, t, x, &ns [i])
		     || ml_rational_parts (ctx, t, x, &ns [i], &ds [i]);
		all = ml_poly_mul (ctx, all, ds [i]);
		ok  = ok && all.len <= MAX_POLY_DEGREE + 1;
	}

	/* sum of n_i * (the product of the other denominators) */
	for (uint64_t i = 0; ok && i < count; i++)
	{
		MlPoly term = ns [i];

		for (uint64_t j = 0; j < count; j++)
		{
			term = ml_choose (j == i, term, ml_poly_mul (ctx, term, ds [j]));
		}

		total = ml_poly_add (ctx, total, term);
	}

	*num = total;
	*den = all;
	return ok && ctx->status == ML_OK;
}

/* ---- the driver ---- */

/* A root is kept when the equation is (numerically) satisfied by it. */
static int32_t satisfies (
	MlSymCtx    *ctx,
	const MlSym *f,
	const MlSym *x,
	const MlSym *root
)
{
	MlStepLog *saved = ctx->log;
	double    v;
	int32_t   ok     = 1;

	ctx->log = NULL;

	if (ml_sym_to_double (ctx->arena, ml_sym_substitute (ctx, f, x, root), &v))
	{
		ok = ml_choose (v < 0, -v, v) < 1e-6;
	}

	ctx->log = saved;
	return ok;
}

/* The distinct roots of the equation by the polynomial in an inner part. */
static int32_t by_inner_part (
	MlSymCtx    *ctx,
	const MlSym *e,
	const MlSym *x,
	SymPtrVec   *out
)
{
	const MlSym *w     = ml_sym_var (ctx, ml_slice_cstr ("_w"));
	SymPtrVec   parts;
	int32_t     done   = 0;

	SymPtrVec_init (&parts);
	ml_sym_collect_parts (ctx, e, x, &parts);

	for (uint64_t i = 0; i < parts.len && !done && ctx->status == ML_OK; i++)
	{
		const MlSym *u = parts.data [i];
		const MlSym *g = ml_sym_replace (ctx, e, u, w);
		MlPoly      p;

		/* exp (2x) is exp (x)^2, and so on for the small multiples */
		for (int64_t k = 2; k <= 6 && u->kind == ML_SYM_FUNC; k++)
		{
			if (ml_slice_equal (u->name, ml_slice_cstr ("exp")))
			{
				g = ml_sym_replace (
					ctx,
					g,
					ml_sym_call (
						ctx,
						u->name,
						ml_sym_mul (ctx, int_node (ctx, k), u->kids [0])
					),
					ml_sym_pow (ctx, w, int_node (ctx, k))
				);
			}
		}

		g = ml_sym_expand (ctx, g);

		if (!ml_sym_has_var (g, x->name) && ml_poly_from_sym (ctx, g, w, &p)
		    && p.len > 1)
		{
			SymPtrVec ws;
			int32_t   solved;

			SymPtrVec_init (&ws);
			solved = poly_roots (ctx, p, &ws);

			for (uint64_t k = 0; solved && k < ws.len; k++)
			{
				invert (ctx, u, ws.data [k], x, out);
			}

			done = solved;
			SymPtrVec_free (&ws);
		}
	}

	SymPtrVec_free (&parts);
	return done;
}

MlSymSolution ml_sym_solve (MlSymCtx *ctx, const MlSym *f, const MlSym *x)
{
	MlSymSolution sol =
	{
		.found = 0,
		.set   = NULL
	};

	const MlSym *e = ml_sym_expand (ctx, f);
	SymPtrVec   roots;
	SymPtrVec   kept;
	MlPoly      p;
	MlPoly      num = ml_poly_zero ();
	MlPoly      den = ml_poly_zero ();
	int32_t     ok = 0;
	int32_t     fraction = 0;

	SymPtrVec_init (&roots);
	SymPtrVec_init (&kept);

	if (!ml_sym_has_var (e, x->name))
	{
		/* no unknown: all x, or none */
		ok = !ml_sym_is_zero (e);
	}

	else if (ml_poly_from_sym (ctx, e, x, &p))
	{
		ok = poly_roots (ctx, p, &roots) || composite_roots (ctx, p, x, &roots);
	}

	else if (together (ctx, e, x, &num, &den))
	{
		fraction = 1;
		ok       = poly_roots (ctx, num, &roots);
	}

	if (!ok && ctx->status == ML_OK && ml_sym_has_var (e, x->name))
	{
		roots.len = 0;
		ok        = by_inner_part (ctx, e, x, &roots);
	}

	for (uint64_t i = 0; ok && i < roots.len && ctx->status == ML_OK; i++)
	{
		const MlSym *r     = roots.data [i];
		int32_t     pole   = 0;

		if (fraction)
		{
			const MlSym *dsym = ml_poly_to_sym (ctx, den, x);

			pole = ml_sym_is_zero (ml_sym_substitute (ctx, dsym, x, r));
		}

		if (!pole && satisfies (ctx, e, x, r))
		{
			add (ctx, &kept, r);
		}
	}

	if (ok && ctx->status == ML_OK)
	{
		sol.found = 1;
		sol.set   = ml_sym_set (ctx, kept.data, kept.len);
	}

	SymPtrVec_free (&kept);
	SymPtrVec_free (&roots);
	return sol;
}

/* ---- the set of solutions ---- */

/* Increasing by value when both have one, else by the order of trees. */
static int32_t item_order (MlSymCtx *ctx, const MlSym *a, const MlSym *b)
{
	double  x;
	double  y;
	int32_t order;

	if (
		   ml_sym_to_double (ctx->arena, a, &x)
		&& ml_sym_to_double (ctx->arena, b, &y)
	)
	{
		order = ml_choose (x < y, -1, ml_choose (x > y, 1, 0));
	}

	else
	{
		order = ml_sym_compare (ctx, a, b);
	}

	return order;
}

const MlSym *ml_sym_set (MlSymCtx *ctx, const MlSym *const *items, uint64_t n)
{
	SymPtrVec   sorted;
	const MlSym *out;

	SymPtrVec_init (&sorted);

	/* insertion into a sorted list, dropping repeats */
	for (uint64_t i = 0; i < n && ctx->status == ML_OK; i++)
	{
		uint64_t at    = 0;
		int32_t  known = 0;

		while (at < sorted.len
		       && item_order (ctx, sorted.data [at], items [i]) < 0)
		{
			at++;
		}

		known = at < sorted.len
		        && item_order (ctx, sorted.data [at], items [i]) == 0;

		if (!known)
		{
			if (SymPtrVec_push (&sorted, items [i]) != ML_OK)
			{
				ctx->status = ML_ERR_NOMEM;
			}

			for (uint64_t k = sorted.len - 1; k > at && ctx->status == ML_OK;
			     k--)
			{
				sorted.data [k] = sorted.data [k - 1];
			}

			if (ctx->status == ML_OK)
			{
				sorted.data [at] = items [i];
			}
		}
	}

	out = ml_sym_func_n (ctx, ml_slice_cstr ("set"), sorted.data, sorted.len);
	SymPtrVec_free (&sorted);
	return out;
}
