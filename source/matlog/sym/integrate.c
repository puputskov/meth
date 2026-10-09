#include "internal.h"

/*
 * The antiderivative of the cases a table can give: after the expression is
 * multiplied out it is a sum of terms; a term is a constant factor (anything
 * without the variable) times a product of factors with it. The product is
 * looked up:
 *
 *	1                       x
 *	u^n  (u = a x + b)      u^(n+1)/(a (n+1)),  ln|u| / a for n = -1
 *	b^u, e^u                b^u / (a ln b),  e^u / a
 *	sin cos tan exp ln
 *	atan asin acos  of u    the usual ones, divided by a
 *	1/(x^2 + k)             atan(x/m)/m  when k = m^2 is a square
 *
 * Anything else (a product of two functions of x, a function of a function)
 * is not found: integration by parts and substitution are not here yet.
 */

static const MlSym *int_node (MlSymCtx *ctx, int64_t n)
{
	return ml_sym_int (ctx, n);
}

static int32_t is_name (MlSlice name, const char *text)
{
	return ml_slice_equal (name, ml_slice_cstr (text));
}

static const MlSym *ln_of (MlSymCtx *ctx, const MlSym *u)
{
	return ml_sym_call (ctx, ml_slice_cstr ("ln"), u);
}

/* u = a x + b with a and b free of x and a not zero? */
static int32_t linear_parts (
	MlSymCtx    *ctx,
	const MlSym *u,
	const MlSym *x,
	const MlSym **a,
	const MlSym **b
)
{
	int32_t ok     = 1;
	uint64_t count = ml_choose (u->kind == ML_SYM_ADD, u->count, 1);

	*a = int_node (ctx, 0);
	*b = int_node (ctx, 0);

	for (uint64_t i = 0; i < count && ok; i++)
	{
		const MlSym *t = ml_choose (u->kind == ML_SYM_ADD, u->kids [i], u);

		if (!ml_sym_has_var (t, x->name))
		{
			*b = ml_sym_add (ctx, *b, t);
		}

		else if (ml_sym_compare (ctx, t, x) == 0)
		{
			*a = ml_sym_add (ctx, *a, int_node (ctx, 1));
		}

		else if (t->kind == ML_SYM_MUL)
		{
			int32_t     seen = 0;
			SymPtrVec   rest;

			SymPtrVec_init (&rest);

			for (uint64_t k = 0; k < t->count && ok; k++)
			{
				if (ml_sym_compare (ctx, t->kids [k], x) == 0 && !seen)
				{
					seen = 1;
				}

				else if (ml_sym_has_var (t->kids [k], x->name))
				{
					ok = 0;
				}

				else if (SymPtrVec_push (&rest, t->kids [k]) != ML_OK)
				{
					ctx->status = ML_ERR_NOMEM;
					ok          = 0;
				}
			}

			ok = ok && seen;

			if (ok)
			{
				*a = ml_sym_add (
					ctx,
					*a,
					ml_sym_product (ctx, rest.data, rest.len)
				);
			}

			SymPtrVec_free (&rest);
		}

		else
		{
			ok = 0;
		}
	}

	return ok && !ml_sym_is_zero (*a);
}

/* The integral of the factor g of a term, as far as the table goes. */
static const MlSym *table (
	MlSymCtx    *ctx,
	const MlSym *g,
	const MlSym *x,
	MlRule      *rule
)
{
	const MlSym *out = NULL;
	const MlSym *a;
	const MlSym *b;

	*rule = ML_RULE_NONE;

	if (ml_sym_compare (ctx, g, int_node (ctx, 1)) == 0)
	{
		*rule = ML_RULE_INT_CONSTANT;
		out   = x;
	}

	else if (
		   g->kind == ML_SYM_POW
		&& g->kids [1]->kind == ML_SYM_NUM
		&& linear_parts (ctx, g->kids [0], x, &a, &b)
	)
	{
		const MlSym *n = g->kids [1];

		if (ml_sym_is_zero (ml_sym_add (ctx, n, int_node (ctx, 1))))
		{
			*rule = ML_RULE_INT_RECIPROCAL;
			out   = ml_sym_div (
				ctx,
				ln_of (
					ctx,
					ml_sym_call (ctx, ml_slice_cstr ("abs"), g->kids [0])
				),
				a
			);
		}

		else
		{
			const MlSym *n1 = ml_sym_add (ctx, n, int_node (ctx, 1));

			*rule = ML_RULE_INT_POWER;
			out   = ml_sym_div (
				ctx,
				ml_sym_pow (ctx, g->kids [0], n1),
				ml_sym_mul (ctx, a, n1)
			);
		}
	}

	else if (
		   g->kind == ML_SYM_POW
		&& g->kids [0]->kind == ML_SYM_FUNC
		&& (
			   is_name (g->kids [0]->name, "sin")
			|| is_name (g->kids [0]->name, "cos")
		)
		&& ml_sym_compare (ctx, g->kids [1], int_node (ctx, 2)) == 0
		&& linear_parts (ctx, g->kids [0]->kids [0], x, &a, &b)
	)
	{
		/* sin^2 u = (1 - cos 2u) / 2 and cos^2 u = (1 + cos 2u) / 2 */
		int32_t     is_sin = is_name (g->kids [0]->name, "sin");
		const MlSym *u2    = ml_sym_mul (ctx, int_node (ctx, 2), g->kids [0]->kids [0]);
		const MlSym *s2    = ml_sym_div (
			ctx,
			ml_sym_call (ctx, ml_slice_cstr ("sin"), u2),
			ml_sym_mul (ctx, int_node (ctx, 4), a)
		);

		*rule = ML_RULE_INT_HALF_ANGLE;
		out   = ml_sym_div (ctx, x, int_node (ctx, 2));
		out   = ml_choose (is_sin, ml_sym_sub (ctx, out, s2), ml_sym_add (ctx, out, s2));
	}

	else if (ml_sym_compare (ctx, g, x) == 0)
	{
		*rule = ML_RULE_INT_POWER;
		out   = ml_sym_div (
			ctx,
			ml_sym_pow (ctx, x, int_node (ctx, 2)),
			int_node (ctx, 2)
		);
	}

	else if (
		   g->kind == ML_SYM_POW
		&& !ml_sym_has_var (g->kids [0], x->name)
		&& linear_parts (ctx, g->kids [1], x, &a, &b)
	)
	{
		/* b^u / (a ln b), and e^u / a */
		int32_t is_e = ml_sym_is_var (g->kids [0], ml_slice_cstr ("e"));

		*rule = ML_RULE_INT_EXPONENTIAL;
		out   = ml_sym_div (
			ctx,
			g,
			ml_choose (is_e, a, ml_sym_mul (ctx, a, ln_of (ctx, g->kids [0])))
		);
	}

	else if (
		   g->kind == ML_SYM_FUNC
		&& linear_parts (ctx, g->kids [0], x, &a, &b)
	)
	{
		const MlSym *u = g->kids [0];

		if (is_name (g->name, "sin"))
		{
			*rule = ML_RULE_INT_SINE;
			out   = ml_sym_div (
				ctx,
				ml_sym_neg (ctx, ml_sym_call (ctx, ml_slice_cstr ("cos"), u)),
				a
			);
		}

		else if (is_name (g->name, "cos"))
		{
			*rule = ML_RULE_INT_COSINE;
			out   = ml_sym_div (
				ctx,
				ml_sym_call (ctx, ml_slice_cstr ("sin"), u),
				a
			);
		}

		else if (is_name (g->name, "tan"))
		{
			*rule = ML_RULE_INT_TANGENT;
			out   = ml_sym_div (
				ctx,
				ml_sym_neg (
					ctx,
					ln_of (
						ctx,
						ml_sym_call (
							ctx,
							ml_slice_cstr ("abs"),
							ml_sym_call (ctx, ml_slice_cstr ("cos"), u)
						)
					)
				),
				a
			);
		}

		else if (is_name (g->name, "exp"))
		{
			*rule = ML_RULE_INT_EXPONENTIAL;
			out   = ml_sym_div (ctx, g, a);
		}

		else if (is_name (g->name, "ln"))
		{
			*rule = ML_RULE_INT_LOGARITHM;
			out   = ml_sym_div (
				ctx,
				ml_sym_sub (ctx, ml_sym_mul (ctx, u, g), u),
				a
			);
		}

		else if (is_name (g->name, "atan"))
		{
			*rule = ML_RULE_INT_ARCTANGENT;
			out   = ml_sym_div (
				ctx,
				ml_sym_sub (
					ctx,
					ml_sym_mul (ctx, u, g),
					ml_sym_div (
						ctx,
						ln_of (
							ctx,
							ml_sym_add (
								ctx,
								int_node (ctx, 1),
								ml_sym_pow (ctx, u, int_node (ctx, 2))
							)
						),
						int_node (ctx, 2)
					)
				),
				a
			);
		}
	}

	return out;
}

/* 1/(x^2 + k) for a square k: atan(x/m)/m. */
static const MlSym *inverse_quadratic (
	MlSymCtx    *ctx,
	const MlSym *g,
	const MlSym *x
)
{
	const MlSym *out = NULL;

	if (
		   g->kind == ML_SYM_POW
		&& ml_sym_is_one (ml_sym_neg (ctx, g->kids [1]))
		&& g->kids [0]->kind == ML_SYM_ADD
		&& g->kids [0]->count == 2
		&& g->kids [0]->kids [1]->kind == ML_SYM_NUM
		&& ml_rat_sign (g->kids [0]->kids [1]->num) > 0
		&& ml_sym_compare (
			ctx,
			g->kids [0]->kids [0],
			ml_sym_pow (ctx, x, int_node (ctx, 2))
		) == 0
	)
	{
		MlRatSqrt m = ml_rat_sqrt_exact (
			ctx->arena,
			g->kids [0]->kids [1]->num
		);

		if (m.exact)
		{
			const MlSym *root = ml_sym_num (ctx, m.value);

			out = ml_sym_div (
				ctx,
				ml_sym_call (
					ctx,
					ml_slice_cstr ("atan"),
					ml_sym_div (ctx, x, root)
				),
				root
			);
		}
	}

	return out;
}

/* The antiderivative of a product of factors that all depend on x. */
const MlSym *ml_int_table (
	MlSymCtx    *ctx,
	const MlSym *f,
	const MlSym *x,
	MlRule      *rule
)
{
	const MlSym *out = NULL;

	*rule = ML_RULE_NONE;

	if (f->kind != ML_SYM_MUL)
	{
		out = table (ctx, f, x, rule);

		if (out == NULL)
		{
			out = inverse_quadratic (ctx, f, x);
			*rule = ml_choose (
				out != NULL,
				ML_RULE_INT_ARCTANGENT,
				ML_RULE_NONE
			);
		}
	}

	return out;
}

/* Splits a term into the factors without x and the product with x. */
void ml_int_split (
	MlSymCtx    *ctx,
	const MlSym *t,
	const MlSym *x,
	const MlSym **free_part,
	const MlSym **x_part
)
{
	SymPtrVec constant;
	SymPtrVec varying;

	SymPtrVec_init (&constant);
	SymPtrVec_init (&varying);

	for (
		uint64_t i = 0;
		i < ml_choose (t->kind == ML_SYM_MUL, t->count, 1);
		i++
	)
	{
		const MlSym *f = ml_choose (t->kind == ML_SYM_MUL, t->kids [i], t);

		if (
			SymPtrVec_push (
				ml_sym_has_var (f, x->name) ? &varying : &constant,
				f
			) != ML_OK
		)
		{
			ctx->status = ML_ERR_NOMEM;
		}
	}

	*free_part = ml_sym_product (ctx, constant.data, constant.len);
	*x_part    = ml_sym_product (ctx, varying.data, varying.len);
	SymPtrVec_free (&constant);
	SymPtrVec_free (&varying);
}

/* "integral of f dx = F   (rule)" into the log. */
static void log_integral (
	MlSymCtx    *ctx,
	const MlSym *f,
	const MlSym *x,
	const MlSym *result,
	MlRule      rule
)
{
	if (ctx->log != NULL && ctx->status == ML_OK && ctx->log->status == ML_OK)
	{
		MlStr text;

		ml_str_init (&text);
		ctx->log->status = ml_str_append (
			&text,
			ml_slice_cstr ("\xE2\x88\xAB ")
		);

		if (ctx->log->status == ML_OK)
		{
			ctx->log->status = ml_sym_format (&text, f);
		}

		if (ctx->log->status == ML_OK)
		{
			ctx->log->status = ml_str_append (&text, ml_slice_cstr (" d"));
		}

		if (ctx->log->status == ML_OK)
		{
			ctx->log->status = ml_sym_format (&text, x);
		}

		if (ctx->log->status == ML_OK)
		{
			ctx->log->status = ml_str_append (&text, ml_slice_cstr (" = "));
		}

		if (ctx->log->status == ML_OK)
		{
			ctx->log->status = ml_sym_format (&text, result);
		}

		if (ctx->log->status == ML_OK)
		{
			ctx->log->status = ml_str_appendf (
				&text,
				"   (%s)",
				ml_rule_name (rule)
			);
		}

		if (ctx->log->status == ML_OK)
		{
			ml_steplog_line (ctx->log, ml_str_view (text));
		}

		ml_str_free (&text);
	}
}

/*
 * The antiderivative of any expression by the table and the partial
 * fractions only, term by term; NULL when a term needs more.
 */
const MlSym *ml_int_simple (MlSymCtx *ctx, const MlSym *g, const MlSym *x)
{
	const MlSym *expanded = ml_sym_expand (ctx, g);
	uint64_t    count     = ml_choose (
		expanded->kind == ML_SYM_ADD,
		expanded->count,
		1
	);
	SymPtrVec   parts;
	int32_t     all       = 1;
	const MlSym *result   = NULL;

	SymPtrVec_init (&parts);

	for (uint64_t i = 0; i < count && all && ctx->status == ML_OK; i++)
	{
		const MlSym *t = ml_choose (
			expanded->kind == ML_SYM_ADD,
			expanded->kids [i],
			expanded
		);
		const MlSym *c;
		const MlSym *f;
		MlRule      rule = ML_RULE_NONE;
		const MlSym *big;

		ml_int_split (ctx, t, x, &c, &f);
		big = ml_int_table (ctx, f, x, &rule);

		if (big == NULL)
		{
			big = ml_int_rational_sym (ctx, f, x);
		}

		if (big == NULL || SymPtrVec_push (&parts, ml_sym_mul (ctx, c, big)) != ML_OK)
		{
			all = 0;
		}
	}

	if (all && ctx->status == ML_OK)
	{
		result = ml_sym_sum (ctx, parts.data, parts.len);
	}

	SymPtrVec_free (&parts);
	return result;
}

/*
 * The antiderivative of one product: the table, then partial fractions,
 * substitution and integration by parts; each of the last three is checked by
 * differentiating before it is believed.
 */
static const MlSym *ml_int_term (
	MlSymCtx    *ctx,
	const MlSym *f,
	const MlSym *x,
	MlRule      *rule
)
{
	const MlSym *big = ml_int_table (ctx, f, x, rule);

	if (big == NULL)
	{
		big = ml_int_rational_sym (ctx, f, x);

		if (big != NULL && !ml_int_verify (ctx, big, f, x))
		{
			big = NULL;
		}

		*rule = ml_choose (
			big != NULL,
			ML_RULE_INT_PARTIAL_FRACTIONS,
			ML_RULE_NONE
		);
	}

	if (big == NULL && ctx->status == ML_OK)
	{
		big = ml_int_subst (ctx, f, x, rule);
	}

	if (big == NULL && ctx->status == ML_OK)
	{
		big = ml_int_parts (ctx, f, x, rule);
	}

	return big;
}

MlSymIntegral ml_sym_integrate (MlSymCtx *ctx, const MlSym *s, const MlSym *x)
{
	MlSymIntegral res =
	{
		.found = 0,
		.value = ml_sym_failed ()
	};
	const MlSym *expanded = ml_sym_expand (ctx, s);
	uint64_t    count     = ml_choose (
		expanded->kind == ML_SYM_ADD,
		expanded->count,
		1
	);
	SymPtrVec   parts;
	int32_t     all       = 1;

	SymPtrVec_init (&parts);

	if (count > 1 && ctx->log != NULL)
	{
		ml_steplog_line (
			ctx->log,
			ml_slice_cstr ("linearity: integrate term by term")
		);
	}

	for (uint64_t i = 0; i < count && all && ctx->status == ML_OK; i++)
	{
		const MlSym *t = ml_choose (
			expanded->kind == ML_SYM_ADD,
			expanded->kids [i],
			expanded
		);
		const MlSym *c;
		const MlSym *f;
		MlRule      rule = ML_RULE_NONE;
		const MlSym *big;

		ml_int_split (ctx, t, x, &c, &f);
		big = ml_int_term (ctx, f, x, &rule);

		if (big == NULL)
		{
			all = 0;
		}

		else
		{
			const MlSym *term = ml_sym_mul (ctx, c, big);

			log_integral (ctx, t, x, term, rule);

			if (SymPtrVec_push (&parts, term) != ML_OK)
			{
				ctx->status = ML_ERR_NOMEM;
			}
		}
	}

	if (all && ctx->status == ML_OK)
	{
		res.found = 1;
		res.value = ml_sym_sum (ctx, parts.data, parts.len);
	}

	SymPtrVec_free (&parts);
	return res;
}
