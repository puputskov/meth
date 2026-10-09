#include "internal.h"

#include <string.h>

/*
 * The canonical form. The constructors here take canonical parts and give a
 * canonical result, and never call each other in a circle:
 *
 *	ml_sym_sum      uses nothing of the others
 *	pow_core        uses nothing of the others
 *	ml_sym_add      uses ml_sym_sum
 *	ml_sym_product  uses ml_sym_add and pow_core
 *	ml_sym_pow      uses pow_core and ml_sym_product
 *	ml_sym_call     uses ml_sym_product and ml_sym_pow
 *
 * Canonical means: a sum is flat, its terms are sorted (higher degree first,
 * a number last), terms that differ in the number only are one term; a product
 * is flat, starts with its number when that is not 1, the factors are sorted,
 * equal bases are one factor with the exponents added; a power never has the
 * number 0 or 1 as exponent, never a power or (with a whole exponent) a
 * product as base, and numbers are folded.
 */

ML_VEC_DEFINE (RatVec, MlRat)

static MlRat one_rat (MlSymCtx *ctx)
{
	return ml_rat_from_i64 (ctx->arena, 1);
}

static const MlSym *int_node (MlSymCtx *ctx, int64_t n)
{
	return ml_sym_int (ctx, n);
}

/* ---- ordering ---- */

/*
 * The degree of one factor: a symbol or a function is 1 (pi and e are
 * numbers: 0), a power of one with a number as exponent is that number.
 */
static MlRat degree_of_factor (MlSymCtx *ctx, const MlSym *s)
{
	MlRat d = ml_rat_zero (ctx->arena);

	if (s->kind == ML_SYM_VAR)
	{
		d = ml_choose (
			   ml_slice_equal (s->name, ml_slice_cstr ("pi"))
			|| ml_slice_equal (s->name, ml_slice_cstr ("e")),
			d,
			one_rat (ctx)
		);
	}

	else if (s->kind == ML_SYM_FUNC)
	{
		d = one_rat (ctx);
	}

	else if (
		   s->kind == ML_SYM_POW
		&& s->kids [1]->kind == ML_SYM_NUM
		&& (
			   s->kids [0]->kind == ML_SYM_VAR
			|| s->kids [0]->kind == ML_SYM_FUNC
		)
	)
	{
		d = ml_rat_mul (
			ctx->arena,
			ml_choose (
				s->kids [0]->kind == ML_SYM_FUNC,
				one_rat (ctx),
				degree_of_factor (ctx, s->kids [0])
			),
			s->kids [1]->num
		);
	}

	return d;
}

/* The degree of a term: the sum over the factors of a product. */
static MlRat degree_of (MlSymCtx *ctx, const MlSym *s)
{
	MlRat d = degree_of_factor (ctx, s);

	if (s->kind == ML_SYM_MUL)
	{
		d = ml_rat_zero (ctx->arena);

		for (uint64_t i = 0; i < s->count; i++)
		{
			d = ml_rat_add (ctx->arena, d, degree_of_factor (ctx, s->kids [i]));
		}
	}

	return d;
}

static uint64_t factor_count (const MlSym *t)
{
	return ml_choose (t->kind == ML_SYM_MUL, t->count, 1);
}

static const MlSym *factor_at (const MlSym *t, uint64_t i)
{
	return ml_choose (t->kind == ML_SYM_MUL, t->kids [i], t);
}

/*
 * Terms of the same degree: by their factors from the first on, the one with
 * the earlier symbol first (x y before y y), and for the same symbol the
 * higher power first (x^2 before x y).
 */
static int32_t monomial_order (MlSymCtx *ctx, const MlSym *a, const MlSym *b)
{
	int32_t  order = 0;
	uint64_t n     = ml_choose (
		factor_count (a) < factor_count (b),
		factor_count (a),
		factor_count (b)
	);

	for (uint64_t i = 0; i < n && order == 0; i++)
	{
		const MlSym *fa = factor_at (a, i);
		const MlSym *fb = factor_at (b, i);
		const MlSym *ba = ml_choose (fa->kind == ML_SYM_POW, fa->kids [0], fa);
		const MlSym *bb = ml_choose (fb->kind == ML_SYM_POW, fb->kids [0], fb);

		order = ml_sym_compare (ctx, ba, bb);

		if (order == 0 && fa->kind == ML_SYM_POW && fb->kind == ML_SYM_POW)
		{
			order = -ml_sym_compare (ctx, fa->kids [1], fb->kids [1]);
		}

		else if (order == 0)
		{
			order = ml_choose (
				fa->kind == fb->kind,
				0,
				ml_choose (fa->kind == ML_SYM_POW, -1, 1)
			);
		}
	}

	return ml_choose (order != 0, order, ml_sym_compare (ctx, a, b));
}

/* Terms: higher degree first, then by their factors. */
static int32_t term_order (MlSymCtx *ctx, const MlSym *a, const MlSym *b)
{
	int32_t order = -ml_rat_cmp (
		ctx->arena,
		degree_of (ctx, a),
		degree_of (ctx, b)
	);

	return ml_choose (order != 0, order, monomial_order (ctx, a, b));
}

static uint32_t base_rank (const MlSym *base)
{
	uint32_t rank = 4;

	rank = ml_choose (base->kind == ML_SYM_VAR, 0, rank);
	rank = ml_choose (base->kind == ML_SYM_FUNC, 1, rank);
	rank = ml_choose (base->kind == ML_SYM_ADD, 2, rank);
	rank = ml_choose (base->kind == ML_SYM_NUM, 3, rank);
	return rank;
}

/* Factors: by base (symbols, functions, sums, numbers), then exponent. */
static int32_t factor_order (MlSymCtx *ctx, const MlSym *a, const MlSym *b)
{
	const MlSym *base_a = ml_choose (a->kind == ML_SYM_POW, a->kids [0], a);
	const MlSym *base_b = ml_choose (b->kind == ML_SYM_POW, b->kids [0], b);
	int32_t     order   = 0;

	if (base_rank (base_a) != base_rank (base_b))
	{
		order = ml_choose (base_rank (base_a) < base_rank (base_b), -1, 1);
	}

	else
	{
		order = ml_sym_compare (ctx, base_a, base_b);
	}

	return ml_choose (order != 0, order, ml_sym_compare (ctx, a, b));
}

typedef enum Order
{
	ORDER_TERMS = 0,
	ORDER_FACTORS
} Order;

/* Insertion sort of the first n entries of v, and of coeffs along with them. */
static void sort_syms (
	MlSymCtx     *ctx,
	SymPtrVec    *v,
	RatVec       *coeffs,
	Order        order
)
{
	for (uint64_t i = 1; i < v->len; i++)
	{
		uint64_t j = i;

		while (
			   j > 0
			&& ml_choose (
				order == ORDER_TERMS,
				term_order (ctx, v->data [j - 1], v->data [j]),
				factor_order (ctx, v->data [j - 1], v->data [j])
			) > 0
		)
		{
			const MlSym *t = v->data [j - 1];

			v->data [j - 1] = v->data [j];
			v->data [j]     = t;

			if (coeffs != NULL)
			{
				MlRat c = coeffs->data [j - 1];

				coeffs->data [j - 1] = coeffs->data [j];
				coeffs->data [j]     = c;
			}

			j--;
		}
	}
}

/* ---- sums ---- */

/*
 * Splits a term into its number and the rest: 3x is (3, x), 2xy is (2, xy),
 * x is (1, x) and a number is (the number, none).
 */
static const MlSym *split_term (MlSymCtx *ctx, const MlSym *t, MlRat *coeff)
{
	const MlSym *rest = t;

	*coeff = one_rat (ctx);

	if (t->kind == ML_SYM_NUM)
	{
		*coeff = t->num;
		rest   = NULL;
	}

	else if (t->kind == ML_SYM_MUL && t->kids [0]->kind == ML_SYM_NUM)
	{
		*coeff = t->kids [0]->num;
		rest   = ml_choose (
			t->count == 2,
			t->kids [1],
			ml_sym_raw (ctx, ML_SYM_MUL, t->kids + 1, t->count - 1)
		);
	}

	return rest;
}

/* coeff times rest, rest being a canonical term without a number. */
static const MlSym *with_coeff (MlSymCtx *ctx, MlRat coeff, const MlSym *rest)
{
	const MlSym *n    = ml_sym_num (ctx, coeff);
	const MlSym *out  = rest;

	if (!ml_sym_is_one (n))
	{
		SymPtrVec kids;

		SymPtrVec_init (&kids);

		if (SymPtrVec_push (&kids, n) != ML_OK)
		{
			ctx->status = ML_ERR_NOMEM;
		}

		if (rest->kind == ML_SYM_MUL)
		{
			for (uint64_t i = 0; i < rest->count; i++)
			{
				if (SymPtrVec_push (&kids, rest->kids [i]) != ML_OK)
				{
					ctx->status = ML_ERR_NOMEM;
				}
			}
		}

		else if (SymPtrVec_push (&kids, rest) != ML_OK)
		{
			ctx->status = ML_ERR_NOMEM;
		}

		out = ml_sym_raw (ctx, ML_SYM_MUL, kids.data, kids.len);
		SymPtrVec_free (&kids);
	}

	return out;
}

const MlSym *ml_sym_sum (MlSymCtx *ctx, const MlSym *const *terms, uint64_t n)
{
	SymPtrVec rests;
	RatVec    coeffs;
	MlRat     constant = ml_rat_zero (ctx->arena);
	SymPtrVec out;
	const MlSym *result;
	MlResult  r = ML_OK;

	SymPtrVec_init (&rests);
	RatVec_init (&coeffs);
	SymPtrVec_init (&out);

	for (uint64_t i = 0; i < n && r == ML_OK; i++)
	{
		const MlSym *t    = terms [i];
		uint64_t    count = ml_choose (t->kind == ML_SYM_ADD, t->count, 1);

		/* a sum inside a sum is spread out */
		for (uint64_t k = 0; k < count && r == ML_OK; k++)
		{
			const MlSym *one_term = ml_choose (
				t->kind == ML_SYM_ADD,
				t->kids [k],
				t
			);
			MlRat        c;
			const MlSym *rest = split_term (ctx, one_term, &c);
			int32_t      found = 0;

			if (rest == NULL)
			{
				constant = ml_rat_add (ctx->arena, constant, c);
			}

			for (uint64_t g = 0; rest != NULL && g < rests.len && !found; g++)
			{
				if (ml_sym_compare (ctx, rests.data [g], rest) == 0)
				{
					coeffs.data [g] = ml_rat_add (
						ctx->arena,
						coeffs.data [g],
						c
					);
					found           = 1;
				}
			}

			if (rest != NULL && !found)
			{
				r = SymPtrVec_push (&rests, rest);
				r = ml_choose (r == ML_OK, RatVec_push (&coeffs, c), r);
			}
		}
	}

	/* drop the terms that cancelled, sort the others */
	for (uint64_t g = 0; g < rests.len && r == ML_OK; g++)
	{
		if (ml_rat_is_zero (coeffs.data [g]))
		{
			rests.data [g]  = NULL;
		}
	}

	{
		uint64_t kept = 0;

		for (uint64_t g = 0; g < rests.len; g++)
		{
			if (rests.data [g] != NULL)
			{
				rests.data [kept]  = rests.data [g];
				coeffs.data [kept] = coeffs.data [g];
				kept++;
			}
		}

		rests.len  = kept;
		coeffs.len = kept;
	}

	sort_syms (ctx, &rests, &coeffs, ORDER_TERMS);

	for (uint64_t g = 0; g < rests.len && r == ML_OK; g++)
	{
		r = SymPtrVec_push (
			&out,
			with_coeff (ctx, coeffs.data [g], rests.data [g])
		);
	}

	if (r == ML_OK && !ml_rat_is_zero (constant))
	{
		r = SymPtrVec_push (&out, ml_sym_num (ctx, constant));
	}

	if (r != ML_OK && ctx->status == ML_OK)
	{
		ctx->status = r;
	}

	result = ml_choose (
		out.len == 0,
		int_node (ctx, 0),
		ml_choose (
			out.len == 1,
			out.data [0],
			ml_sym_raw (ctx, ML_SYM_ADD, out.data, out.len)
		)
	);

	SymPtrVec_free (&rests);
	RatVec_free (&coeffs);
	SymPtrVec_free (&out);
	return ml_choose (ctx->status == ML_OK, result, ml_sym_failed ());
}

const MlSym *ml_sym_add (MlSymCtx *ctx, const MlSym *a, const MlSym *b)
{
	const MlSym *terms [2] = { a, b };

	return ml_sym_sum (ctx, terms, 2);
}

/* ---- powers ---- */

/* The whole exponent of a power as a small number, if it is one. */
static int32_t small_int (const MlSym *s, int64_t *value)
{
	return ml_sym_to_i64 (s, value) && *value > -100000 && *value < 100000;
}

/*
 * base to the power exp for a base that is not a product or a power (or, for
 * a fractional exponent, anything). No other constructor is used.
 */
static const MlSym *pow_core (
	MlSymCtx    *ctx,
	const MlSym *base,
	const MlSym *exp
)
{
	const MlSym *result = NULL;
	int64_t     n       = 0;

	if (ml_sym_is_zero (exp))
	{
		result = int_node (ctx, 1);
	}

	else if (ml_sym_is_one (exp) || ml_sym_is_one (base))
	{
		result = base;
	}

	else if (base->kind == ML_SYM_NUM && ml_sym_is_zero (base))
	{
		if (exp->kind == ML_SYM_NUM && ml_rat_sign (exp->num) < 0)
		{
			ctx->status = ML_ERR_DIVZERO;
		}

		result = ml_choose (
			exp->kind == ML_SYM_NUM,
			base,
			NULL
		);
	}

	else if (base->kind == ML_SYM_NUM && small_int (exp, &n))
	{
		result = ml_sym_num (ctx, ml_rat_pow (ctx->arena, base->num, n));
	}

	else if (base->kind == ML_SYM_NUM && exp->kind == ML_SYM_NUM)
	{
		/* a root: exact when both parts are perfect squares */
		MlRat half = ml_rat_make (
			ctx->arena,
			ml_big_from_u64 (ctx->arena, 1),
			ml_big_from_u64 (ctx->arena, 2)
		);

		if (ml_rat_cmp (ctx->arena, exp->num, half) == 0)
		{
			MlRatSqrt root = ml_rat_sqrt_exact (ctx->arena, base->num);

			result = ml_choose (
				root.exact,
				ml_sym_num (ctx, root.value),
				NULL
			);
		}
	}

	if (result == NULL)
	{
		const MlSym *kids [2] = { base, exp };

		result = ml_sym_raw (ctx, ML_SYM_POW, kids, 2);
	}

	return result;
}

/* ---- products ---- */

const MlSym *ml_sym_product (
	MlSymCtx           *ctx,
	const MlSym *const *factors,
	uint64_t           n
)
{
	SymPtrVec work;
	SymPtrVec next;
	MlRat     coeff   = one_rat (ctx);
	MlResult  r       = ML_OK;
	int32_t   again   = 1;
	uint64_t  passes  = 0;
	const MlSym *result;

	SymPtrVec_init (&work);
	SymPtrVec_init (&next);

	for (uint64_t i = 0; i < n && r == ML_OK; i++)
	{
		r = SymPtrVec_push (&work, factors [i]);
	}

	/* one pass: flatten, take the numbers, collect equal bases */
	while (r == ML_OK && again && passes < 8)
	{
		SymPtrVec bases;
		SymPtrVec exps;

		SymPtrVec_init (&bases);
		SymPtrVec_init (&exps);
		again = 0;
		passes++;
		SymPtrVec_clear (&next);

		for (uint64_t i = 0; i < work.len && r == ML_OK; i++)
		{
			const MlSym *f     = work.data [i];
			uint64_t    count = ml_choose (f->kind == ML_SYM_MUL, f->count, 1);

			for (uint64_t k = 0; k < count && r == ML_OK; k++)
			{
				const MlSym *one_factor = ml_choose (
					f->kind == ML_SYM_MUL,
					f->kids [k],
					f
				);

				if (one_factor->kind == ML_SYM_NUM)
				{
					coeff = ml_rat_mul (ctx->arena, coeff, one_factor->num);
				}

				else
				{
					const MlSym *base = ml_choose (
						one_factor->kind == ML_SYM_POW,
						one_factor->kids [0],
						one_factor
					);
					const MlSym *exp  = ml_choose (
						one_factor->kind == ML_SYM_POW,
						one_factor->kids [1],
						int_node (ctx, 1)
					);
					int32_t     found = 0;

					for (uint64_t g = 0; g < bases.len && !found; g++)
					{
						if (ml_sym_compare (ctx, bases.data [g], base) == 0)
						{
							exps.data [g] = ml_sym_add (
								ctx,
								exps.data [g],
								exp
							);
							found         = 1;
						}
					}

					if (!found)
					{
						r = SymPtrVec_push (&bases, base);
						r = ml_choose (
							r == ML_OK,
							SymPtrVec_push (&exps, exp),
							r
						);
					}
				}
			}
		}

		for (uint64_t g = 0; g < bases.len && r == ML_OK; g++)
		{
			const MlSym *f = pow_core (ctx, bases.data [g], exps.data [g]);

			if (f->kind == ML_SYM_NUM)
			{
				coeff = ml_rat_mul (ctx->arena, coeff, f->num);
			}

			else
			{
				again = again || f->kind == ML_SYM_MUL;
				r     = SymPtrVec_push (&next, f);
			}
		}

		SymPtrVec_free (&bases);
		SymPtrVec_free (&exps);

		/* the rebuilt factors are the work of the next pass */
		{
			SymPtrVec t = work;

			work = next;
			next = t;
		}
	}

	if (r != ML_OK && ctx->status == ML_OK)
	{
		ctx->status = r;
	}

	sort_syms (ctx, &work, NULL, ORDER_FACTORS);

	if (ml_rat_is_zero (coeff) || work.len == 0)
	{
		result = ml_sym_num (
			ctx,
			ml_choose (work.len == 0, coeff, ml_rat_zero (ctx->arena))
		);
	}

	else if (
		   work.len == 1
		&& work.data [0]->kind == ML_SYM_ADD
		&& !ml_sym_is_one (ml_sym_num (ctx, coeff))
	)
	{
		/* a number times a sum goes into the terms: 2(x + 1) = 2x + 2 */
		SymPtrVec terms;
		const MlSym *sum = work.data [0];

		SymPtrVec_init (&terms);

		for (uint64_t i = 0; i < sum->count; i++)
		{
			MlRat        c;
			const MlSym *rest = split_term (ctx, sum->kids [i], &c);
			MlRat        k    = ml_rat_mul (ctx->arena, c, coeff);

			if (
				SymPtrVec_push (
					&terms,
					ml_choose (
						rest == NULL,
						ml_sym_num (ctx, k),
						with_coeff (ctx, k, rest)
					)
				) != ML_OK
			)
			{
				ctx->status = ML_ERR_NOMEM;
			}
		}

		result = ml_sym_sum (ctx, terms.data, terms.len);
		SymPtrVec_free (&terms);
	}

	else
	{
		SymPtrVec all;

		SymPtrVec_init (&all);

		if (!ml_sym_is_one (ml_sym_num (ctx, coeff)))
		{
			SymPtrVec_push (&all, ml_sym_num (ctx, coeff));
		}

		for (uint64_t i = 0; i < work.len; i++)
		{
			SymPtrVec_push (&all, work.data [i]);
		}

		result = ml_choose (
			all.len == 1,
			all.data [0],
			ml_sym_raw (ctx, ML_SYM_MUL, all.data, all.len)
		);
		SymPtrVec_free (&all);
	}

	SymPtrVec_free (&work);
	SymPtrVec_free (&next);
	return ml_choose (ctx->status == ML_OK, result, ml_sym_failed ());
}

const MlSym *ml_sym_mul (MlSymCtx *ctx, const MlSym *a, const MlSym *b)
{
	const MlSym *f [2] = { a, b };

	return ml_sym_product (ctx, f, 2);
}

const MlSym *ml_sym_neg (MlSymCtx *ctx, const MlSym *a)
{
	return ml_sym_mul (ctx, int_node (ctx, -1), a);
}

const MlSym *ml_sym_sub (MlSymCtx *ctx, const MlSym *a, const MlSym *b)
{
	return ml_sym_add (ctx, a, ml_sym_neg (ctx, b));
}

const MlSym *ml_sym_pow (MlSymCtx *ctx, const MlSym *a, const MlSym *b)
{
	const MlSym *base   = a;
	const MlSym *exp    = b;
	int64_t     n       = 0;
	const MlSym *result = NULL;

	/* (x^p)^n = x^(p n) for a whole n: as often as it applies */
	while (
		   base->kind == ML_SYM_POW
		&& exp->kind == ML_SYM_NUM
		&& ml_rat_is_integer (exp->num)
	)
	{
		exp  = ml_sym_mul (ctx, base->kids [1], exp);
		base = base->kids [0];
	}

	if (base->kind == ML_SYM_MUL && exp->kind == ML_SYM_NUM
		&& ml_rat_is_integer (exp->num) && ml_sym_to_i64 (exp, &n))
	{
		/* (x y)^n = x^n y^n */
		SymPtrVec parts;

		SymPtrVec_init (&parts);

		for (uint64_t i = 0; i < base->count; i++)
		{
			if (
				SymPtrVec_push (
					&parts,
					pow_core (ctx, base->kids [i], exp)
				) != ML_OK
			)
			{
				ctx->status = ML_ERR_NOMEM;
			}
		}

		result = ml_sym_product (ctx, parts.data, parts.len);
		SymPtrVec_free (&parts);
	}

	else
	{
		result = pow_core (ctx, base, exp);
	}

	return ml_choose (ctx->status == ML_OK, result, ml_sym_failed ());
}

const MlSym *ml_sym_div (MlSymCtx *ctx, const MlSym *a, const MlSym *b)
{
	return ml_sym_mul (ctx, a, ml_sym_pow (ctx, b, int_node (ctx, -1)));
}

/* ---- functions ---- */

static int32_t is_name (MlSlice name, const char *text)
{
	return ml_slice_equal (name, ml_slice_cstr (text));
}

const MlSym *ml_sym_call (MlSymCtx *ctx, MlSlice name, const MlSym *arg)
{
	const MlSym *result = NULL;
	int32_t     odd     = is_name (name, "sin") || is_name (name, "tan")
		|| is_name (name, "asin") || is_name (name, "atan");
	int32_t     even    = is_name (name, "cos") || is_name (name, "abs");
	int32_t     negated = (
		   arg->kind == ML_SYM_NUM && ml_rat_sign (arg->num) < 0
	) || (
		   arg->kind == ML_SYM_MUL
		&& arg->kids [0]->kind == ML_SYM_NUM
		&& ml_rat_sign (arg->kids [0]->num) < 0
	);

	if (ml_sym_is_zero (arg))
	{
		result = ml_choose (
			is_name (name, "cos") || is_name (name, "exp"),
			int_node (ctx, 1),
			ml_choose (
				is_name (name, "sin") || is_name (name, "tan")
					|| is_name (name, "asin") || is_name (name, "atan")
					|| is_name (name, "abs"),
				int_node (ctx, 0),
				NULL
			)
		);
	}

	else if (ml_sym_is_one (arg) && is_name (name, "ln"))
	{
		result = int_node (ctx, 0);
	}

	else if (arg->kind == ML_SYM_NUM && is_name (name, "abs"))
	{
		MlRat m = arg->num;

		result = ml_sym_num (
			ctx,
			ml_choose (ml_rat_sign (m) < 0, ml_rat_neg (ctx->arena, m), m)
		);
	}

	else if (is_name (name, "exp") && arg->kind == ML_SYM_FUNC
		&& is_name (arg->name, "ln"))
	{
		result = arg->kids [0];
	}

	else if (is_name (name, "ln") && arg->kind == ML_SYM_FUNC
		&& is_name (arg->name, "exp"))
	{
		result = arg->kids [0];
	}

	else if ((odd || even) && negated)
	{
		/* sin(-x) = -sin(x), cos(-x) = cos(x) */
		const MlSym *inner = ml_sym_func (ctx, name, ml_sym_neg (ctx, arg));

		result = ml_choose (odd, ml_sym_neg (ctx, inner), inner);
	}

	if (result == NULL)
	{
		result = ml_sym_func (ctx, name, arg);
	}

	return ml_choose (ctx->status == ML_OK, result, ml_sym_failed ());
}

/* ---- expanding ---- */

/* The terms of a sum, or the node itself as the only term. */
static uint64_t term_count (const MlSym *s)
{
	return ml_choose (s->kind == ML_SYM_ADD, s->count, 1);
}

static const MlSym *term_at (const MlSym *s, uint64_t i)
{
	return ml_choose (s->kind == ML_SYM_ADD, s->kids [i], s);
}

/* a times b with the sums multiplied out. */
static const MlSym *mul_expand (MlSymCtx *ctx, const MlSym *a, const MlSym *b)
{
	const MlSym *result;
	uint64_t    na = term_count (a);
	uint64_t    nb = term_count (b);

	if (a->kind != ML_SYM_ADD && b->kind != ML_SYM_ADD)
	{
		result = ml_sym_mul (ctx, a, b);
	}

	else if (na * nb > ML_SYM_BUDGET / 4)
	{
		ctx->status = ML_ERR_LIMIT;
		result      = ml_sym_failed ();
	}

	else
	{
		SymPtrVec products;

		SymPtrVec_init (&products);

		for (uint64_t i = 0; i < na && ctx->status == ML_OK; i++)
		{
			for (uint64_t k = 0; k < nb; k++)
			{
				if (
					SymPtrVec_push (
						&products,
						ml_sym_mul (ctx, term_at (a, i), term_at (b, k))
					) != ML_OK
				)
				{
					ctx->status = ML_ERR_NOMEM;
				}
			}
		}

		result = ml_sym_sum (ctx, products.data, products.len);
		SymPtrVec_free (&products);
	}

	return result;
}

/* A sum to a whole power n >= 2, by repeated squaring. */
static const MlSym *pow_expand (MlSymCtx *ctx, const MlSym *base, uint64_t n)
{
	const MlSym *result = NULL;
	const MlSym *square = base;
	uint64_t    left    = n;

	while (left > 0 && ctx->status == ML_OK)
	{
		if (left & 1)
		{
			result = ml_choose (
				result == NULL,
				square,
				mul_expand (ctx, result, square)
			);
		}

		left >>= 1;

		if (left > 0)
		{
			square = mul_expand (ctx, square, square);
		}
	}

	return ml_choose (ctx->status == ML_OK, result, ml_sym_failed ());
}

/* ---- the rewrite at one node ---- */

static const char *rule_name (
	MlSymMode   mode,
	const MlSym *orig,
	const MlSym *out
)
{
	const char *name = "simplify";

	if (orig->kind == ML_SYM_ADD)
	{
		name = ml_choose (
			out->kind == ML_SYM_NUM,
			"add the numbers",
			"collect like terms"
		);
	}

	else if (orig->kind == ML_SYM_MUL)
	{
		name = ml_choose (
			out->kind == ML_SYM_NUM,
			"multiply the numbers",
			ml_choose (
				mode == ML_SYM_EXPAND,
				"multiply out",
				"combine the factors"
			)
		);
	}

	else if (orig->kind == ML_SYM_POW)
	{
		name = ml_choose (
			mode == ML_SYM_EXPAND,
			"expand the power",
			"simplify the power"
		);
	}

	else if (orig->kind == ML_SYM_FUNC)
	{
		name = "evaluate the function";
	}

	return name;
}

/* "rule: before = after" into the step log. */
static void log_rewrite (
	MlSymCtx    *ctx,
	const MlSym *orig,
	const MlSym *before,
	const MlSym *after
)
{
	if (ctx->log != NULL && ctx->status == ML_OK && ctx->log->status == ML_OK)
	{
		MlStr text;

		ml_str_init (&text);
		ctx->log->status = ml_str_appendf (
			&text,
			"%s: ",
			rule_name (ctx->mode, orig, after)
		);

		if (ctx->log->status == ML_OK)
		{
			ctx->log->status = ml_sym_format (&text, before);
		}

		if (ctx->log->status == ML_OK)
		{
			ctx->log->status = ml_str_append (&text, ml_slice_cstr (" = "));
		}

		if (ctx->log->status == ML_OK)
		{
			ctx->log->status = ml_sym_format (&text, after);
		}

		if (ctx->log->status == ML_OK)
		{
			ml_steplog_line (ctx->log, ml_str_view (text));
		}

		ml_str_free (&text);
	}
}

const MlSym *ml_sym_rewrite (
	MlSymCtx           *ctx,
	const MlSym        *orig,
	const MlSym *const *kids,
	uint64_t           count
)
{
	const MlSym *out    = orig;
	int32_t     expand  = ctx->mode == ML_SYM_EXPAND;

	if (orig->kind == ML_SYM_VAR)
	{
		out = ml_choose (
			   ctx->mode == ML_SYM_SUBSTITUTE
			&& ctx->subst_var != NULL
			&& ml_sym_compare (ctx, orig, ctx->subst_var) == 0,
			ctx->subst_val,
			orig
		);
	}

	else if (orig->kind == ML_SYM_ADD)
	{
		out = ml_sym_sum (ctx, kids, count);
	}

	else if (orig->kind == ML_SYM_MUL)
	{
		if (expand)
		{
			out = kids [0];

			for (uint64_t i = 1; i < count; i++)
			{
				out = mul_expand (ctx, out, kids [i]);
			}
		}

		else
		{
			out = ml_sym_product (ctx, kids, count);
		}
	}

	else if (orig->kind == ML_SYM_POW)
	{
		int64_t n = 0;

		if (
			   expand
			&& kids [0]->kind == ML_SYM_ADD
			&& ml_sym_to_i64 (kids [1], &n)
			&& n >= 2
			&& n <= 64
		)
		{
			out = pow_expand (ctx, kids [0], (uint64_t)n);
		}

		else
		{
			out = ml_sym_pow (ctx, kids [0], kids [1]);
		}
	}

	else if (orig->kind == ML_SYM_FUNC)
	{
		out = ml_sym_call (ctx, orig->name, kids [0]);
	}

	/* the step: this node changed given what its kids became */
	if (
		   ctx->log != NULL
		&& orig->kind != ML_SYM_NUM
		&& orig->kind != ML_SYM_VAR
	)
	{
		const MlSym *before = ml_choose (
			orig->kind == ML_SYM_FUNC,
			ml_sym_func (ctx, orig->name, kids [0]),
			ml_sym_raw (ctx, orig->kind, kids, count)
		);

		if (ml_sym_compare (ctx, before, out) != 0)
		{
			log_rewrite (ctx, orig, before, out);
		}
	}

	return out;
}

static const MlSym *run_map (MlSymCtx *ctx, const MlSym *s, MlSymMode mode)
{
	MlSymMode saved = ctx->mode;
	const MlSym *out;

	ctx->mode = mode;
	out       = ml_sym_map (ctx, s);
	ctx->mode = saved;
	return out;
}

const MlSym *ml_sym_simplify (MlSymCtx *ctx, const MlSym *s)
{
	return run_map (ctx, s, ML_SYM_REBUILD);
}

const MlSym *ml_sym_expand (MlSymCtx *ctx, const MlSym *s)
{
	return run_map (ctx, s, ML_SYM_EXPAND);
}

const MlSym *ml_sym_substitute (
	MlSymCtx    *ctx,
	const MlSym *s,
	const MlSym *x,
	const MlSym *value
)
{
	const MlSym *saved_var = ctx->subst_var;
	const MlSym *saved_val = ctx->subst_val;
	const MlSym *out;

	ctx->subst_var = x;
	ctx->subst_val = value;
	out            = run_map (ctx, s, ML_SYM_SUBSTITUTE);
	ctx->subst_var = saved_var;
	ctx->subst_val = saved_val;
	return out;
}
