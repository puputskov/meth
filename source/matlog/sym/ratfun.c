#include "internal.h"

#include <matlog/math/matrix.h>

/*
 * The integral of a quotient of polynomials by partial fractions: the
 * polynomial part of the division by the power rule, and for the proper part
 * a sum A / (x - r)^j over the rational roots r of the denominator, and
 * (B x + C) / q for one irreducible quadratic q. The unknown A, B and C come
 * from a linear system, solved exactly. A denominator with any other factor
 * (a cubic without rational roots, a quadratic that is squared) is left to the
 * other methods.
 */

static const MlSym *int_node (MlSymCtx *ctx, int64_t n)
{
	return ml_sym_int (ctx, n);
}

static const MlSym *rat_node (MlSymCtx *ctx, MlRat r)
{
	return ml_sym_num (ctx, r);
}

/* ln |u| */
static const MlSym *ln_abs (MlSymCtx *ctx, const MlSym *u)
{
	return ml_sym_call (
		ctx,
		ml_slice_cstr ("ln"),
		ml_sym_call (ctx, ml_slice_cstr ("abs"), u)
	);
}

/* The polynomial part: the power rule on every term of the quotient. */
static void add_polynomial_part (
	MlSymCtx    *ctx,
	MlPoly      q,
	const MlSym *x,
	SymPtrVec   *parts
)
{
	for (uint64_t i = 0; i < q.len; i++)
	{
		MlRat k = ml_rat_div (
			ctx->arena,
			q.c [i],
			ml_rat_from_i64 (ctx->arena, (int64_t)(i + 1))
		);
		const MlSym *term = ml_sym_mul (
			ctx,
			rat_node (ctx, k),
			ml_sym_pow (ctx, x, int_node (ctx, (int64_t)(i + 1)))
		);

		if (SymPtrVec_push (parts, term) != ML_OK)
		{
			ctx->status = ML_ERR_NOMEM;
		}
	}
}

/*
 * The square root of a positive rational in its simplest form: k sqrt (m) / d
 * with the square factors taken out of m (sqrt (8) is 2 sqrt (2)).
 */
const MlSym *ml_rat_root_node (MlSymCtx *ctx, MlRat r)
{
	MlBig       prod = ml_big_mul (ctx->arena, r.num, r.den);
	MlU64Result m    = ml_big_to_u64 (prod);
	MlU64Result d    = ml_big_to_u64 (r.den);
	const MlSym *out;

	if (m.ok && d.ok && d.value > 0)
	{
		uint64_t rest   = m.value;
		uint64_t factor = 1;

		for (uint64_t p = 2; p * p <= rest && p < 100000; p++)
		{
			while (rest % (p * p) == 0)
			{
				rest   /= p * p;
				factor *= p;
			}
		}

		out = ml_sym_mul (
			ctx,
			ml_sym_div (
				ctx,
				ml_sym_int (ctx, (int64_t)factor),
				ml_sym_int (ctx, (int64_t)d.value)
			),
			ml_sym_pow (
				ctx,
				ml_sym_int (ctx, (int64_t)rest),
				ml_sym_div (ctx, int_node (ctx, 1), int_node (ctx, 2))
			)
		);
	}

	else
	{
		out = ml_sym_pow (
			ctx,
			rat_node (ctx, r),
			ml_sym_div (ctx, int_node (ctx, 1), int_node (ctx, 2))
		);
	}

	return out;
}

/* The integral of 1 / q for the monic quadratic q = x^2 + p x + s. */
static const MlSym *inverse_quadratic (
	MlSymCtx    *ctx,
	MlRat       p,
	MlRat       s,
	const MlSym *x,
	int32_t     *signed_log
)
{
	MlRat four  = ml_rat_from_i64 (ctx->arena, 4);
	MlRat delta = ml_rat_sub (
		ctx->arena,
		ml_rat_mul (ctx->arena, four, s),
		ml_rat_mul (ctx->arena, p, p)
	);
	const MlSym *lin   = ml_sym_add (
		ctx,
		ml_sym_mul (ctx, int_node (ctx, 2), x),
		rat_node (ctx, p)
	);
	const MlSym *out;

	if (ml_rat_sign (delta) > 0)
	{
		const MlSym *root = ml_rat_root_node (ctx, delta);

		*signed_log = 0;
		out         = ml_sym_mul (
			ctx,
			ml_sym_div (ctx, int_node (ctx, 2), root),
			ml_sym_call (ctx, ml_slice_cstr ("atan"), ml_sym_div (ctx, lin, root))
		);
	}

	else
	{
		const MlSym *root = ml_rat_root_node (ctx, ml_rat_neg (ctx->arena, delta));

		*signed_log = 1;
		out         = ml_sym_mul (
			ctx,
			ml_sym_div (ctx, int_node (ctx, 1), root),
			ln_abs (
				ctx,
				ml_sym_div (
					ctx,
					ml_sym_sub (ctx, lin, root),
					ml_sym_add (ctx, lin, root)
				)
			)
		);
	}

	return out;
}

/* A basis polynomial: the monic denominator divided by (x - r)^j. */
static MlPoly basis_linear (
	MlSymCtx *ctx,
	MlPoly   d0,
	MlRat    root,
	uint64_t j
)
{
	MlPoly lin  = ml_poly_make (ctx, 2);
	MlPoly pw   = ml_poly_make (ctx, 1);
	MlPoly q;
	MlPoly r;

	if (lin.len == 2 && pw.len == 1)
	{
		lin.c [0] = ml_rat_neg (ctx->arena, root);
		lin.c [1] = ml_rat_from_i64 (ctx->arena, 1);
		pw.c [0]  = ml_rat_from_i64 (ctx->arena, 1);
	}

	for (uint64_t i = 0; i < j; i++)
	{
		pw = ml_poly_mul (ctx, pw, lin);
	}

	ml_poly_divmod (ctx, d0, pw, &q, &r);
	return q;
}

/* Adds the column of a basis polynomial to the system. */
static void put_column (MlMat *a, uint64_t col, MlPoly basis, uint64_t n)
{
	for (uint64_t k = 0; k < n && k < basis.len; k++)
	{
		ml_mat_set (a, k, col, basis.c [k]);
	}
}

/*
 * The system a x = b for the unknown coefficients: one column for every
 * A / (x - r)^j and, with a quadratic, the two of (B x + C) / q. d0 is the
 * monic denominator, rest the quadratic factor (len 3) or 1.
 */
static uint64_t build_system (
	MlSymCtx          *ctx,
	MlPoly            d0,
	MlPoly            scaled,
	const MlPolyRoots *roots,
	MlMat             *a,
	MlMat             *b
)
{
	uint64_t n   = d0.len - 1;
	uint64_t col = 0;

	for (uint64_t i = 0; i < roots->count; i++)
	{
		for (uint64_t j = 1; j <= roots->mult [i]; j++)
		{
			put_column (a, col, basis_linear (ctx, d0, roots->root [i], j), n);
			col++;
		}
	}

	if (roots->rest.len == 3)
	{
		MlPoly base;
		MlPoly rem;
		MlPoly xp = ml_poly_make (ctx, 2);

		ml_poly_divmod (ctx, d0, roots->rest, &base, &rem);

		if (xp.len == 2)
		{
			xp.c [1] = ml_rat_from_i64 (ctx->arena, 1);
		}

		put_column (a, col, ml_poly_mul (ctx, xp, base), n);
		put_column (a, col + 1, base, n);
		col += 2;
	}

	for (uint64_t k = 0; k < n && k < scaled.len; k++)
	{
		ml_mat_set (b, k, 0, scaled.c [k]);
	}

	return col;
}

static void push_part (MlSymCtx *ctx, SymPtrVec *parts, const MlSym *s)
{
	if (SymPtrVec_push (parts, s) != ML_OK)
	{
		ctx->status = ML_ERR_NOMEM;
	}
}

/* The integral of A / (x - r)^j. */
static const MlSym *linear_term (
	MlSymCtx    *ctx,
	MlRat       coef,
	MlRat       root,
	uint64_t    j,
	const MlSym *x
)
{
	const MlSym *base = ml_sym_sub (ctx, x, rat_node (ctx, root));
	const MlSym *out;

	if (j == 1)
	{
		out = ml_sym_mul (ctx, rat_node (ctx, coef), ln_abs (ctx, base));
	}

	else
	{
		int64_t     m = 1 - (int64_t)j;
		const MlSym *power = ml_sym_pow (ctx, base, int_node (ctx, m));

		out = ml_sym_div (
			ctx,
			ml_sym_mul (ctx, rat_node (ctx, coef), power),
			int_node (ctx, m)
		);
	}

	return out;
}

/* The integral of (B x + C) / q for the monic quadratic q. */
static void quadratic_terms (
	MlSymCtx    *ctx,
	MlPoly      q,
	MlRat       bb,
	MlRat       cc,
	const MlSym *x,
	SymPtrVec   *parts
)
{
	MlRat       p       = q.c [1];
	MlRat       s       = q.c [0];
	MlRat       half    = ml_rat_make (
		ctx->arena,
		ml_big_from_i64 (ctx->arena, 1),
		ml_big_from_i64 (ctx->arena, 2)
	);
	int32_t     is_log  = 0;
	const MlSym *one_over = inverse_quadratic (ctx, p, s, x, &is_log);
	const MlSym *qs     = ml_poly_to_sym (ctx, q, x);
	MlRat       rest    = ml_rat_sub (
		ctx->arena,
		cc,
		ml_rat_mul (ctx->arena, ml_rat_mul (ctx->arena, bb, p), half)
	);
	const MlSym *logq   = ml_choose (
		is_log,
		ln_abs (ctx, qs),
		ml_sym_call (ctx, ml_slice_cstr ("ln"), qs)
	);

	push_part (
		ctx,
		parts,
		ml_sym_mul (ctx, rat_node (ctx, ml_rat_mul (ctx->arena, bb, half)), logq)
	);
	push_part (ctx, parts, ml_sym_mul (ctx, rat_node (ctx, rest), one_over));
}

/* The proper fraction r / den, den of degree >= 1; 0 when it does not work. */
static int32_t proper_fraction (
	MlSymCtx    *ctx,
	MlPoly      r,
	MlPoly      den,
	const MlSym *x,
	SymPtrVec   *parts
)
{
	MlPolyRoots roots  = ml_poly_roots (ctx, den);
	MlRat       lead   = ml_poly_lead (den);
	uint64_t    n      = den.len - 1;
	int32_t     ok     = roots.ok && (roots.rest.len == 1 || roots.rest.len == 3);

	if (ok)
	{
		MlPoly d0     = ml_poly_scale (ctx, den, ml_rat_recip (ctx->arena, lead));
		MlPoly scaled = ml_poly_scale (ctx, r, ml_rat_recip (ctx->arena, lead));
		MlMat  a      = ml_mat_zero (ctx->arena, n, n);
		MlMat  b      = ml_mat_zero (ctx->arena, n, 1);
		uint64_t cols = build_system (ctx, d0, scaled, &roots, &a, &b);
		MlMat  sol    = ml_mat_solve (ctx->arena, a, b);
		uint64_t col  = 0;

		ok = cols == n && ml_mat_ok (sol);

		for (uint64_t i = 0; ok && i < roots.count; i++)
		{
			for (uint64_t j = 1; j <= roots.mult [i]; j++)
			{
				push_part (
					ctx,
					parts,
					linear_term (ctx, sol.cells [col], roots.root [i], j, x)
				);
				col++;
			}
		}

		if (ok && roots.rest.len == 3)
		{
			quadratic_terms (
				ctx,
				roots.rest,
				sol.cells [col],
				sol.cells [col + 1],
				x,
				parts
			);
		}
	}

	return ok;
}

const MlSym *ml_int_rational (
	MlSymCtx    *ctx,
	MlPoly      num,
	MlPoly      den,
	const MlSym *x
)
{
	const MlSym *result = NULL;
	MlPoly      q       = ml_poly_zero ();
	MlPoly      r       = ml_poly_zero ();
	SymPtrVec   parts;
	int32_t     ok;

	SymPtrVec_init (&parts);
	den = ml_poly_trim (den);
	ok  = den.len >= 2 && den.len <= MAX_POLY_DEGREE + 1;

	if (ok)
	{
		ml_poly_divmod (ctx, num, den, &q, &r);
		add_polynomial_part (ctx, q, x, &parts);
		ok = r.len == 0 || proper_fraction (ctx, r, den, x, &parts);
	}

	if (ok && ctx->status == ML_OK)
	{
		result = ml_sym_sum (ctx, parts.data, parts.len);
	}

	SymPtrVec_free (&parts);
	return result;
}

/* The product of the base^k of the denominator factors, as a polynomial. */
static int32_t denominator_poly (
	MlSymCtx    *ctx,
	const MlSym *f,
	const MlSym *x,
	MlPoly      *den,
	SymPtrVec   *numerators
)
{
	uint64_t count = ml_choose (f->kind == ML_SYM_MUL, f->count, 1);
	int32_t  ok    = 1;
	int32_t  found = 0;
	MlPoly   total = ml_poly_make (ctx, 1);

	if (total.len == 1)
	{
		total.c [0] = ml_rat_from_i64 (ctx->arena, 1);
	}

	for (uint64_t i = 0; i < count && ok && ctx->status == ML_OK; i++)
	{
		const MlSym *g = ml_choose (f->kind == ML_SYM_MUL, f->kids [i], f);
		int64_t     k  = 0;

		if (
			   g->kind == ML_SYM_POW
			&& ml_sym_to_i64 (g->kids [1], &k)
			&& k < 0
			&& ml_sym_has_var (g->kids [0], x->name)
		)
		{
			MlPoly base;

			ok = ml_poly_from_sym (
				ctx,
				ml_sym_expand (ctx, g->kids [0]),
				x,
				&base
			);

			for (int64_t m = 0; ok && m < -k && m < MAX_POLY_DEGREE; m++)
			{
				total = ml_poly_mul (ctx, total, base);
			}

			ok    = ok && -k <= MAX_POLY_DEGREE;
			found = 1;
		}

		else if (SymPtrVec_push (numerators, g) != ML_OK)
		{
			ctx->status = ML_ERR_NOMEM;
		}
	}

	*den = total;
	return ok && found && ctx->status == ML_OK;
}

int32_t ml_rational_parts (
	MlSymCtx    *ctx,
	const MlSym *f,
	const MlSym *x,
	MlPoly      *num,
	MlPoly      *den
)
{
	SymPtrVec numerators;
	int32_t   ok = 0;

	SymPtrVec_init (&numerators);

	if (denominator_poly (ctx, f, x, den, &numerators))
	{
		const MlSym *top = ml_sym_expand (
			ctx,
			ml_sym_product (ctx, numerators.data, numerators.len)
		);

		ok = ml_poly_from_sym (ctx, top, x, num) && den->len >= 2;
	}

	SymPtrVec_free (&numerators);
	return ok;
}

const MlSym *ml_int_rational_sym (
	MlSymCtx    *ctx,
	const MlSym *f,
	const MlSym *x
)
{
	const MlSym *result = NULL;
	MlPoly      den;
	MlPoly      num;

	if (ml_rational_parts (ctx, f, x, &num, &den))
	{
		result = ml_int_rational (ctx, num, den, x);
	}

	return result;
}
